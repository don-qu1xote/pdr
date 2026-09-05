#include "scheduling/core/lesson.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace pdr::scheduling {
namespace {

struct Allowed final {
    LessonState from;
    LessonEvent event;
    LessonState to;
};

constexpr std::array<Allowed, 9> kAllowedTransitions{
    Allowed{LessonState::kPlanned, LessonEvent::kConfirm, LessonState::kConfirmed},
    Allowed{LessonState::kPlanned, LessonEvent::kHold, LessonState::kHeld},
    Allowed{LessonState::kPlanned, LessonEvent::kCancel, LessonState::kCancelled},
    Allowed{LessonState::kPlanned, LessonEvent::kMarkNoShow, LessonState::kNoShow},
    Allowed{LessonState::kPlanned, LessonEvent::kReschedule, LessonState::kPlanned},
    Allowed{LessonState::kConfirmed, LessonEvent::kHold, LessonState::kHeld},
    Allowed{LessonState::kConfirmed, LessonEvent::kCancel, LessonState::kCancelled},
    Allowed{LessonState::kConfirmed, LessonEvent::kMarkNoShow, LessonState::kNoShow},
    Allowed{LessonState::kConfirmed, LessonEvent::kReschedule, LessonState::kConfirmed},
};

core::Money Nothing(const core::CurrencyCode& currency) noexcept {
    return core::Money::FromMinorUnits(0, currency);
}

core::Error NotCountable() {
    return core::Error{core::ErrorKind::kValidation,
                       "retention_not_countable",
                       "доля от такой суммы не считается: слишком велика"};
}

Lesson::Change Made(Lesson lesson,
                    core::Money retained,
                    RetentionReason reason,
                    LessonAction action,
                    const core::PersonId& actor,
                    core::Instant now,
                    std::string details) {
    LessonHistoryEntry record{lesson.Tenant(), lesson.Id(), actor, action, now, std::move(details)};
    CancellationOutcome outcome{lesson.State(), std::move(retained), reason};

    return Lesson::Change{std::move(lesson), std::move(outcome), std::move(record)};
}

}  // namespace

std::string_view Name(LessonState state) noexcept {
    switch (state) {
        case LessonState::kPlanned:
            return "planned";
        case LessonState::kConfirmed:
            return "confirmed";
        case LessonState::kHeld:
            return "held";
        case LessonState::kCancelled:
            return "cancelled";
        case LessonState::kNoShow:
            return "no_show";
        case LessonState::kBoundary:
            break;
    }
    return "planned";
}

std::string_view Name(LessonEvent event) noexcept {
    switch (event) {
        case LessonEvent::kConfirm:
            return "confirm";
        case LessonEvent::kHold:
            return "hold";
        case LessonEvent::kCancel:
            return "cancel";
        case LessonEvent::kMarkNoShow:
            return "mark_no_show";
        case LessonEvent::kReschedule:
            return "reschedule";
        case LessonEvent::kBoundary:
            break;
    }
    return "confirm";
}

core::Result<LessonState> Transition(LessonState from, LessonEvent event) {
    for (const auto& allowed : kAllowedTransitions) {
        if (allowed.from == from && allowed.event == event) {
            return allowed.to;
        }
    }

    return core::Error{core::ErrorKind::kConflict,
                       "lesson_transition_not_allowed",
                       "занятие «" + std::string{Name(from)} + "» не отвечает на «" +
                           std::string{Name(event)} + "»"};
}

Lesson::Lesson(core::LessonId id,
               core::TenantId tenant,
               core::PersonId tutor,
               std::vector<core::PersonId> participants,
               core::Instant starts_at,
               Duration duration,
               core::TimeZone zone,
               LessonState state)
    : id_{std::move(id)},
      tenant_{std::move(tenant)},
      tutor_{std::move(tutor)},
      participants_{std::move(participants)},
      starts_at_{starts_at},
      duration_{duration},
      zone_{std::move(zone)},
      state_{state} {}

core::Result<Lesson> Lesson::Schedule(core::LessonId id,
                                      core::TenantId tenant,
                                      core::PersonId tutor,
                                      std::vector<core::PersonId> participants,
                                      core::Instant starts_at,
                                      Duration duration,
                                      core::TimeZone zone,
                                      core::Instant now) {
    if (duration <= Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_duration_not_positive",
                           "занятие нулевой длины — не занятие"};
    }
    if (starts_at <= now) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_starts_in_past",
                           "записаться назад во времени нельзя"};
    }
    if (participants.size() != kParticipantsForNow) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_participants_not_one",
                           "групповые занятия ещё не заведены: участник сегодня ровно один"};
    }
    if (std::find(participants.begin(), participants.end(), tutor) != participants.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_tutor_among_participants",
                           "репетитор ведёт занятие, а не участвует в нём"};
    }

    auto sorted = participants;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_participant_repeated",
                           "один и тот же участник записан дважды"};
    }

    return Lesson{std::move(id),
                  std::move(tenant),
                  std::move(tutor),
                  std::move(participants),
                  starts_at,
                  duration,
                  std::move(zone),
                  LessonState::kPlanned};
}

core::Result<Lesson> Lesson::After(LessonEvent event) const {
    const auto next = Transition(state_, event);
    if (!next.HasValue()) {
        return next.Failure();
    }

    return Lesson{id_, tenant_, tutor_, participants_, starts_at_, duration_, zone_, next.Value()};
}

core::TimeRange Lesson::Span() const {
    return core::TimeRange::Compose(starts_at_, EndsAt()).Value();
}

core::Result<Lesson::Change> Lesson::CancelByStudent(const CancellationPolicy& policy,
                                                     const core::Money& price,
                                                     const core::PersonId& actor,
                                                     core::Instant now) const {
    auto cancelled = After(LessonEvent::kCancel);
    if (!cancelled.HasValue()) {
        return cancelled.Failure();
    }

    if (policy.Free(starts_at_, now)) {
        return Made(cancelled.Value(),
                    Nothing(price.Currency()),
                    RetentionReason::kInsideFreeWindow,
                    LessonAction::kCancelledByStudent,
                    actor,
                    now,
                    std::string{});
    }

    const auto retained = policy.LateRetention().Of(price);
    if (!retained.has_value()) {
        return NotCountable();
    }

    return Made(cancelled.Value(),
                *retained,
                RetentionReason::kLateCancellation,
                LessonAction::kCancelledByStudent,
                actor,
                now,
                std::string{});
}

core::Result<Lesson::Change> Lesson::CancelByTutor(const core::CurrencyCode& currency,
                                                   const core::PersonId& actor,
                                                   core::Instant now) const {
    auto cancelled = After(LessonEvent::kCancel);
    if (!cancelled.HasValue()) {
        return cancelled.Failure();
    }

    return Made(cancelled.Value(),
                Nothing(currency),
                RetentionReason::kTutorCancelled,
                LessonAction::kCancelledByTutor,
                actor,
                now,
                std::string{});
}

core::Result<Lesson::Change> Lesson::Reschedule(const CancellationPolicy& policy,
                                                const core::Money& price,
                                                const core::PersonId& actor,
                                                core::Instant to,
                                                core::Instant now,
                                                std::span<const LessonHistoryEntry> history) const {
    const auto moved = After(LessonEvent::kReschedule);
    if (!moved.HasValue()) {
        return moved.Failure();
    }
    if (to <= now) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_starts_in_past",
                           "записаться назад во времени нельзя"};
    }
    if (to == starts_at_) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_moved_nowhere",
                           "занятие переносят на другое время, а не на то же самое"};
    }

    const Lesson placed{id_, tenant_, tutor_, participants_, to, duration_, zone_, state_};

    const auto moves = std::count_if(history.begin(), history.end(), [](const auto& entry) {
        return entry.action == LessonAction::kRescheduled;
    });
    const std::string details = "was=" + std::to_string(starts_at_.UnixMicros());

    if (moves < policy.FreeReschedules() || policy.Free(starts_at_, now)) {
        return Made(placed,
                    Nothing(price.Currency()),
                    RetentionReason::kFreeReschedule,
                    LessonAction::kRescheduled,
                    actor,
                    now,
                    details);
    }

    const auto retained = policy.LateRetention().Of(price);
    if (!retained.has_value()) {
        return NotCountable();
    }

    return Made(placed,
                *retained,
                RetentionReason::kLateReschedule,
                LessonAction::kRescheduled,
                actor,
                now,
                details);
}

core::Result<Lesson::Change> Lesson::MarkHeld(const core::Money& price,
                                              const core::PersonId& actor,
                                              core::Instant now) const {
    auto held = After(LessonEvent::kHold);
    if (!held.HasValue()) {
        return held.Failure();
    }

    return Made(held.Value(),
                price,
                RetentionReason::kLessonHeld,
                LessonAction::kHeld,
                actor,
                now,
                std::string{});
}

core::Result<Lesson::Change> Lesson::MarkNoShow(const CancellationPolicy& policy,
                                                const core::Money& price,
                                                const core::PersonId& actor,
                                                core::Instant now) const {
    auto missed = After(LessonEvent::kMarkNoShow);
    if (!missed.HasValue()) {
        return missed.Failure();
    }

    const auto retained = policy.NoShowRetention().Of(price);
    if (!retained.has_value()) {
        return NotCountable();
    }

    return Made(missed.Value(),
                *retained,
                RetentionReason::kNoShow,
                LessonAction::kNoShow,
                actor,
                now,
                std::string{});
}

}  // namespace pdr::scheduling
