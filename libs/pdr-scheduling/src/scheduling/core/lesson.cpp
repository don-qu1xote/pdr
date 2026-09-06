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

std::vector<core::PersonId> PeopleOf(const std::vector<Participation>& participants) {
    std::vector<core::PersonId> people;
    people.reserve(participants.size());
    for (const auto& taking : participants) {
        people.push_back(taking.Person());
    }
    return people;
}

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
               std::vector<Participation> participants,
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
                                      std::vector<Participation> participants,
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
    if (participants.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_without_participants",
                           "занятие без единого участника — не занятие"};
    }
    if (participants.size() > kParticipantsForNow) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_group_not_supported",
                           "групповые занятия пока не поддерживаются: участник сегодня "
                           "ровно один"};
    }
    if (std::find_if(participants.begin(), participants.end(), [&tutor](const auto& taking) {
            return taking.Person() == tutor;
        }) != participants.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_tutor_among_participants",
                           "репетитор ведёт занятие, а не участвует в нём"};
    }

    auto people = PeopleOf(participants);
    std::sort(people.begin(), people.end());
    if (std::adjacent_find(people.begin(), people.end()) != people.end()) {
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

    return Made(held.Value().Outcome(Attendance::kAttended, price),
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

    return Made(missed.Value().Outcome(Attendance::kMissed, price),
                *retained,
                RetentionReason::kNoShow,
                LessonAction::kNoShow,
                actor,
                now,
                std::string{});
}

std::vector<core::PersonId> Lesson::People() const {
    return PeopleOf(participants_);
}

const Participation* Lesson::Participating(const core::PersonId& person) const noexcept {
    for (const auto& taking : participants_) {
        if (taking.Person() == person) {
            return &taking;
        }
    }
    return nullptr;
}

core::Result<Lesson> Lesson::With(const Participation& taking) const {
    if (Participating(taking.Person()) == nullptr) {
        return core::Error{core::ErrorKind::kNotFound,
                           "participation_not_found",
                           "этот человек на занятии не значится"};
    }

    std::vector<Participation> after;
    after.reserve(participants_.size());
    for (const auto& kept : participants_) {
        after.push_back(kept.Person() == taking.Person() ? taking : kept);
    }

    return Lesson{id_, tenant_, tutor_, std::move(after), starts_at_, duration_, zone_, state_};
}

Lesson Lesson::Outcome(Attendance attendance, const core::Money& price) const {
    std::vector<Participation> after;
    after.reserve(participants_.size());
    for (const auto& taking : participants_) {
        if (taking.State() == ParticipationState::kWithdrawn) {
            after.push_back(taking);
            continue;
        }
        const auto priced = taking.Priced(price);
        after.push_back(attendance == Attendance::kAttended ? priced.Came() : priced.Missed());
    }

    return Lesson{id_, tenant_, tutor_, std::move(after), starts_at_, duration_, zone_, state_};
}

core::Result<Lesson::Change> Lesson::Withdraw(const CancellationPolicy& policy,
                                              const core::Money& price,
                                              const core::PersonId& participant,
                                              const core::PersonId& actor,
                                              core::Instant now) const {
    const auto* taking = Participating(participant);
    if (taking == nullptr) {
        return core::Error{core::ErrorKind::kNotFound,
                           "participation_not_found",
                           "этот человек на занятии не значится"};
    }
    if (taking->State() == ParticipationState::kWithdrawn) {
        return core::Error{core::ErrorKind::kConflict,
                           "participation_already_withdrawn",
                           "этот человек с занятия уже вышел"};
    }
    if (Staying() <= 1) {
        return core::Error{core::ErrorKind::kConflict,
                           "participation_last_one",
                           "занятие без единого участника — это отмена, а не выход"};
    }

    const bool free = policy.Free(starts_at_, now);
    const auto retained = free ? std::optional<core::Money>{Nothing(price.Currency())}
                               : policy.LateRetention().Of(price);
    if (!retained.has_value()) {
        return NotCountable();
    }
    const auto reason =
        free ? RetentionReason::kInsideFreeWindow : RetentionReason::kLateCancellation;

    auto left = With(taking->Withdrawn());
    if (!left.HasValue()) {
        return left.Failure();
    }

    LessonHistoryEntry record{
        tenant_, id_, actor, LessonAction::kWithdrawn, now, participant.ToString()};

    return Lesson::Change{
        std::move(left.Value()), CancellationOutcome{state_, *retained, reason}, std::move(record)};
}

std::size_t Lesson::Staying() const noexcept {
    return static_cast<std::size_t>(
        std::count_if(participants_.begin(), participants_.end(), [](const Participation& taking) {
            return taking.State() == ParticipationState::kJoined;
        }));
}

}  // namespace pdr::scheduling
