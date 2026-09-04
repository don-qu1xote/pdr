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

constexpr std::array<Allowed, 7> kAllowedTransitions{
    Allowed{LessonState::kPlanned, LessonEvent::kConfirm, LessonState::kConfirmed},
    Allowed{LessonState::kPlanned, LessonEvent::kHold, LessonState::kHeld},
    Allowed{LessonState::kPlanned, LessonEvent::kCancel, LessonState::kCancelled},
    Allowed{LessonState::kPlanned, LessonEvent::kMarkNoShow, LessonState::kNoShow},
    Allowed{LessonState::kConfirmed, LessonEvent::kHold, LessonState::kHeld},
    Allowed{LessonState::kConfirmed, LessonEvent::kCancel, LessonState::kCancelled},
    Allowed{LessonState::kConfirmed, LessonEvent::kMarkNoShow, LessonState::kNoShow},
};

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
               LessonState state)
    : id_{std::move(id)},
      tenant_{std::move(tenant)},
      tutor_{std::move(tutor)},
      participants_{std::move(participants)},
      starts_at_{starts_at},
      duration_{duration},
      state_{state} {}

core::Result<Lesson> Lesson::Schedule(core::LessonId id,
                                      core::TenantId tenant,
                                      core::PersonId tutor,
                                      std::vector<core::PersonId> participants,
                                      core::Instant starts_at,
                                      Duration duration,
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
                  LessonState::kPlanned};
}

core::Result<Lesson> Lesson::After(LessonEvent event) const {
    const auto next = Transition(state_, event);
    if (!next.HasValue()) {
        return next.Failure();
    }

    return Lesson{id_, tenant_, tutor_, participants_, starts_at_, duration_, next.Value()};
}

core::TimeRange Lesson::Span() const {
    return core::TimeRange::Compose(starts_at_, EndsAt()).Value();
}

}  // namespace pdr::scheduling
