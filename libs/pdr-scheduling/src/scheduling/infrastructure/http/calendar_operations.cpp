#include "scheduling/infrastructure/http/calendar_operations.hpp"

#include <string>
#include <utility>

#include <userver/components/component.hpp>

#include "infrastructure/db/postgres_time_zone_rules.hpp"
#include "scheduling/application/issue_calendar_feed.hpp"
#include "scheduling/application/read_calendar_feed.hpp"
#include "scheduling/infrastructure/http/api_mapping.hpp"
#include "scheduling/infrastructure/postgres_calendar_feeds.hpp"
#include "scheduling/infrastructure/postgres_lesson_repository.hpp"
#include "scheduling/infrastructure/postgres_recurrence_repository.hpp"

namespace pdr::scheduling::http {
namespace {

/// СКОЛЬКО КАЛЕНДАРЮ РАЗРЕШЕНО ДЕРЖАТЬ ЛЕНТУ У СЕБЯ.
///
/// Совпадает со сроком обновления, который написан в самой ленте: два разных
/// числа означали бы, что мы просим об одном, а разрешаем другое.
constexpr int kCacheSeconds = kRefreshMinutes * 60;

}  // namespace

CalendarFeedHandler::CalendarFeedHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      digests_{parts.Digests()},
      wording_{parts.Words()} {}

identity::Action CalendarFeedHandler::Wants() const {
    return identity::Action::kViewSchedule;
}

identity::Resource CalendarFeedHandler::About(const userver::server::http::HttpRequest&,
                                              const infrastructure::http::Caller& caller,
                                              const api::Nothing&) const {
    /// Подписка открывает СВОЁ расписание и ничьё больше: человека назвал не
    /// адрес, а секрет в нём, и спрашивается политика о нём же.
    ///
    /// ОБЕ СТОРОНЫ РЕСУРСА — ОДИН ЧЕЛОВЕК, и это не описка. В ленту уезжает
    /// расписание целиком: и занятия, которые он ведёт, и те, на которые он
    /// записан. Назвать одну сторону значило бы спросить политику про половину
    /// того, что отдаём.
    return identity::Resource{caller.tenant, caller.actor, caller.actor};
}

std::string_view CalendarFeedHandler::MediaType() const {
    return "text/calendar; charset=utf-8";
}

core::Result<std::string> CalendarFeedHandler::Run(const Call& call) const {
    PostgresCalendarFeeds feeds{call.session};
    PostgresLessonRepository lessons{call.session};
    PostgresRecurrenceRepository series{call.session};
    infrastructure::db::PostgresTimeZoneRules zones{call.session};

    const ReadCalendarFeed reading{feeds, lessons, series, zones, digests_, call.clock, wording_};

    const auto read =
        reading.Execute(ReadCalendarFeed::Request{call.caller.tenant, call.caller.actor});
    if (!read.HasValue()) {
        return read.Failure();
    }

    /// КЭШИРОВАНИЕ: календари ходят сюда часто и без спроса, и попросить их об
    /// ином нельзя — можно только сказать, до какого момента ответ годен и по
    /// какому отпечатку его узнать.
    call.handed.emplace_back("ETag", "\"" + read.Value().etag + "\"");
    call.handed.emplace_back("Cache-Control", "private, max-age=" + std::to_string(kCacheSeconds));

    return read.Value().calendar;
}

IssueCalendarFeedHandler::IssueCalendarFeedHandler(Parts& parts)
    : AuthorizedHandler{parts.Callers(),
                        parts.Database(),
                        parts.Permissions(),
                        parts.Keys(),
                        parts.Clock(),
                        parts.Lifetime()},
      secrets_{parts.Secrets()},
      digests_{parts.Digests()} {}

identity::Action IssueCalendarFeedHandler::Wants() const {
    return identity::Action::kViewSchedule;
}

identity::Resource IssueCalendarFeedHandler::About(const userver::server::http::HttpRequest&,
                                                   const infrastructure::http::Caller& caller,
                                                   const api::Nothing&) const {
    /// Ссылку человек выдаёт СЕБЕ. Выдать её за другого нельзя вовсе: чужой
    /// подписки в этом обращении не назвать. Ресурс тот же, что у самой ленты:
    /// спрашивать при выдаче о меньшем, чем потом отдаём, нельзя.
    return identity::Resource{caller.tenant, caller.actor, caller.actor};
}

core::Result<api::CalendarSubscription> IssueCalendarFeedHandler::Run(const Call& call) const {
    PostgresCalendarFeeds feeds{call.session};
    const IssueCalendarFeed issuing{feeds, secrets_, digests_, call.clock};

    const auto issued =
        issuing.Execute(IssueCalendarFeed::Request{call.caller.tenant, call.caller.actor});
    if (!issued.HasValue()) {
        return issued.Failure();
    }

    return AsAnswer(call.caller.tenant, issued.Value().secret);
}

CalendarFeedOperation::CalendarFeedOperation(const userver::components::ComponentConfig& config,
                                             const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& CalendarFeedOperation::Handler() const {
    return handler_;
}

userver::yaml_config::Schema CalendarFeedOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

IssueCalendarFeedOperation::IssueCalendarFeedOperation(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : OperationComponent{config, context}, parts_{config, context}, handler_{parts_} {}

const infrastructure::http::Operation& IssueCalendarFeedOperation::Handler() const {
    return handler_;
}

userver::yaml_config::Schema IssueCalendarFeedOperation::GetStaticConfigSchema() {
    return Parts::Schema();
}

}  // namespace pdr::scheduling::http
