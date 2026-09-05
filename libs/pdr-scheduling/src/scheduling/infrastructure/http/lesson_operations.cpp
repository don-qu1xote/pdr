#include "scheduling/infrastructure/http/lesson_operations.hpp"

#include <chrono>
#include <optional>
#include <utility>

#include <userver/components/component.hpp>

#include "scheduling/application/book_lesson.hpp"
#include "scheduling/application/get_lesson.hpp"
#include "scheduling/application/list_lessons.hpp"
#include "scheduling/infrastructure/http/api_mapping.hpp"
#include "scheduling/infrastructure/http/arguments.hpp"
#include "scheduling/infrastructure/postgres_lesson_repository.hpp"

namespace pdr::scheduling::http {
namespace {

/// Пара «чьё расписание и какой стороной» — та же, что видела политика.
///
/// Читается второй раз, и это не повтор работы: политике вопрос задают ДО
/// работы и без права отказать, а здесь тот же аргумент разбирается уже со
/// значением — и негодный превращается в ответ человеку, а не в решение о
/// правах.
struct Asked final {
    core::PersonId whose;
    Side side;
};

core::Result<Asked> WhoseSchedule(const userver::server::http::HttpRequest& request,
                                  const core::PersonId& asking) {
    const auto whose = Whose(request, asking);
    if (!whose.HasValue()) {
        return whose.Failure();
    }
    const auto side = WhichSide(request);
    if (!side.HasValue()) {
        return side.Failure();
    }
    return Asked{whose.Value(), side.Value()};
}

}  // namespace

ListLessonsHandler::ListLessonsHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()} {}

identity::Action ListLessonsHandler::Wants() const {
    return identity::Action::kViewSchedule;
}

identity::Resource ListLessonsHandler::About(const userver::server::http::HttpRequest& request,
                                             const infrastructure::http::Caller& caller,
                                             const api::Nothing&) const {
    return ScheduleOf(request, caller);
}

core::Result<api::Lessons> ListLessonsHandler::Run(const Call& call) const {
    const auto asked = WhoseSchedule(call.request, call.caller.actor);
    if (!asked.HasValue()) {
        return asked.Failure();
    }
    const auto window = Window(call.request);
    if (!window.HasValue()) {
        return window.Failure();
    }

    const PostgresLessonRepository lessons{call.session};
    const ListLessons showing{lessons};

    const auto found = showing.Execute(ListLessons::Request{
        call.caller.tenant, asked.Value().whose, asked.Value().side, window.Value()});
    if (!found.HasValue()) {
        return found.Failure();
    }
    return AsAnswer(found.Value());
}

GetLessonHandler::GetLessonHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()} {}

identity::Action GetLessonHandler::Wants() const {
    return identity::Action::kViewSchedule;
}

identity::Resource GetLessonHandler::About(const userver::server::http::HttpRequest& request,
                                           const infrastructure::http::Caller& caller,
                                           const api::Nothing&) const {
    return ScheduleOf(request, caller);
}

core::Result<api::Lesson> GetLessonHandler::Run(const Call& call) const {
    const auto asked = WhoseSchedule(call.request, call.caller.actor);
    if (!asked.HasValue()) {
        return asked.Failure();
    }
    const auto which = WhichLesson(call.request);
    if (!which.HasValue()) {
        return which.Failure();
    }

    const PostgresLessonRepository lessons{call.session};
    const GetLesson showing{lessons};

    const auto found = showing.Execute(GetLesson::Request{
        call.caller.tenant, asked.Value().whose, asked.Value().side, which.Value()});
    if (!found.HasValue()) {
        return found.Failure();
    }
    return AsAnswer(found.Value());
}

CreateLessonHandler::CreateLessonHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      ids_{parts.Ids()},
      bus_{parts.Bus()} {}

identity::Action CreateLessonHandler::Wants() const {
    return identity::Action::kBookLesson;
}

identity::Resource CreateLessonHandler::About(const userver::server::http::HttpRequest&,
                                              const infrastructure::http::Caller& caller,
                                              const api::NewLesson& body) const {
    return identity::Resource{caller.tenant, AsPerson(body.tutor), AsPerson(body.student)};
}

core::Result<api::Lesson> CreateLessonHandler::Run(const Call& call) const {
    const auto zone = AsZone(call.body.tz);
    if (!zone.HasValue()) {
        return zone.Failure();
    }

    PostgresLessonRepository lessons{call.session};
    const BookLesson booking{lessons, call.clock, ids_, bus_};

    const auto booked =
        booking.Execute(BookLesson::Request{call.caller.tenant,
                                            AsPerson(call.body.tutor),
                                            AsPerson(call.body.student),
                                            core::Instant::FromUnixMicros(call.body.starts_at),
                                            std::chrono::minutes{call.body.minutes},
                                            zone.Value()});
    if (!booked.HasValue()) {
        return booked.Failure();
    }
    return AsAnswer(booked.Value());
}

ListLessonsOperation::ListLessonsOperation(const userver::components::ComponentConfig& config,
                                           const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& ListLessonsOperation::Handler() const {
    return handler_;
}

GetLessonOperation::GetLessonOperation(const userver::components::ComponentConfig& config,
                                       const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& GetLessonOperation::Handler() const {
    return handler_;
}

CreateLessonOperation::CreateLessonOperation(const userver::components::ComponentConfig& config,
                                             const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& CreateLessonOperation::Handler() const {
    return handler_;
}

userver::yaml_config::Schema ListLessonsOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

userver::yaml_config::Schema GetLessonOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

userver::yaml_config::Schema CreateLessonOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

}  // namespace pdr::scheduling::http
