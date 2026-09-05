#include "scheduling/application/cancel_lesson.hpp"

#include <utility>

#include "events/scheduling/lesson_cancelled.hpp"

namespace pdr::scheduling {

CancelLesson::CancelLesson(ports::LessonRepository& lessons,
                           ports::LessonHistory& history,
                           const ports::CancellationPolicies& policies,
                           const application::ports::Clock& clock,
                           events::Bus& bus) noexcept
    : lessons_{lessons}, history_{history}, policies_{policies}, clock_{clock}, bus_{bus} {}

core::Result<CancellationOutcome> CancelLesson::Execute(const Request& request) const {
    const auto found = lessons_.Find(request.tenant, request.lesson);
    if (!found.has_value()) {
        return core::Error{
            core::ErrorKind::kNotFound, "lesson_not_found", "такого занятия здесь нет"};
    }

    const auto now = clock_.Now();

    auto changed = [&]() -> core::Result<Lesson::Change> {
        if (request.by == CancelledBy::kTutor) {
            return found->CancelByTutor(request.price.Currency(), request.actor, now);
        }

        const auto policy = policies_.Of(request.tenant);
        if (!policy.HasValue()) {
            return policy.Failure();
        }
        return found->CancelByStudent(policy.Value(), request.price, request.actor, now);
    }();
    if (!changed.HasValue()) {
        return changed.Failure();
    }

    const auto stored = lessons_.SetState(changed.Value().lesson);
    if (!stored.HasValue()) {
        return stored.Failure();
    }

    const auto written = history_.Record(changed.Value().record);
    if (!written.HasValue()) {
        return written.Failure();
    }

    bus_.Publish(events::scheduling::LessonCancelled{
        events::Envelope{request.tenant, now},
        request.lesson,
        request.actor,
        request.by,
        changed.Value().outcome.retained,
        changed.Value().outcome.reason,
    });

    return changed.Value().outcome;
}

}  // namespace pdr::scheduling
