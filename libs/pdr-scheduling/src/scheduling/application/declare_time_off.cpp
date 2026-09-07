#include "scheduling/application/declare_time_off.hpp"

#include <algorithm>
#include <utility>

#include "events/scheduling/time_off_declared.hpp"
#include "scheduling/core/lesson_state.hpp"

namespace pdr::scheduling {

std::vector<Lesson> LessonsInside(const ports::LessonRepository& lessons,
                                  const core::TenantId& tenant,
                                  const core::PersonId& person,
                                  const core::TimeRange& span) {
    auto found = lessons.OfTutor(tenant, person, span);
    for (auto& lesson : lessons.OfParticipant(tenant, person, span)) {
        const bool known = std::any_of(found.begin(), found.end(), [&](const Lesson& kept) {
            return kept.Id() == lesson.Id();
        });
        if (!known) {
            found.push_back(std::move(lesson));
        }
    }

    found.erase(std::remove_if(found.begin(),
                               found.end(),
                               [](const Lesson& lesson) {
                                   return lesson.State() != LessonState::kPlanned &&
                                          lesson.State() != LessonState::kConfirmed;
                               }),
                found.end());

    std::sort(found.begin(), found.end(), [](const Lesson& left, const Lesson& right) {
        return left.StartsAt() < right.StartsAt();
    });
    return found;
}

DeclareTimeOff::DeclareTimeOff(ports::TimeOffRepository& periods,
                               const ports::LessonRepository& lessons,
                               const ports::LocalDays& days,
                               const application::ports::IdGenerator& ids,
                               const application::ports::Clock& clock,
                               events::Bus& bus) noexcept
    : periods_{periods}, lessons_{lessons}, days_{days}, ids_{ids}, clock_{clock}, bus_{bus} {}

core::Result<DeclareTimeOff::Answer> DeclareTimeOff::Execute(const Request& request) const {
    auto period = TimeOff::Compose(ids_.Next<core::TimeOffId>(),
                                   request.tenant,
                                   request.person,
                                   request.from,
                                   request.to,
                                   request.reason,
                                   request.zone,
                                   request.declared_by);
    if (!period.HasValue()) {
        return period.Failure();
    }

    /// ОТРЕЗОК СЧИТАЕТСЯ ДО ЗАПИСИ, А НЕ ПОСЛЕ. Здесь же выясняется, что зона
    /// названа существующая: `TimeZone::Parse` проверяет форму имени, а
    /// «Europe/Mordor» проходит форму. Порядок не косметический — писать строку,
    /// чтобы через миг отказать по зоне, значит платить за отказ записью.
    const auto span = days_.Between(period.Value().From(), period.Value().To(), request.zone);
    if (!span.HasValue()) {
        return span.Failure();
    }

    const auto now = clock_.Now();

    const auto stored = periods_.Save(period.Value(), now);
    if (!stored.HasValue()) {
        return stored.Failure();
    }

    /// Отрезок уезжает в событие МОМЕНТАМИ: подписчику незачем разбираться в
    /// правилах зоны, издатель уже разобрался. Биллинг слушает именно это,
    /// чтобы не списывать автоплатёж за занятия, которых не будет (PDR-BILL-05).
    bus_.Publish(events::scheduling::TimeOffDeclared{
        events::Envelope{request.tenant, now},
        period.Value().Id(),
        request.person,
        span.Value().From(),
        span.Value().To(),
        request.reason,
    });

    return Answer{period.Value(),
                  LessonsInside(lessons_, request.tenant, request.person, span.Value())};
}

}  // namespace pdr::scheduling
