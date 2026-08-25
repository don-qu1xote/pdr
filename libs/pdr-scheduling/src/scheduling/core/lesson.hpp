#pragma once

#include <chrono>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::scheduling {

/// Занятие в выставленном репетитором слоте.
///
/// Начало — момент в UTC. Местного времени без зоны здесь нет и быть не может:
/// зона живёт отдельным значением и нужна показу, а не расчёту.
class Lesson final {
public:
    using Duration = std::chrono::minutes;

    /// Назначить занятие. `now` приходит из порта часов — домен «сейчас» не
    /// спрашивает ни у кого.
    static core::Result<Lesson> Schedule(core::LessonId id,
                                         core::TenantId tenant,
                                         core::PersonId tutor,
                                         core::PersonId student,
                                         core::Instant starts_at,
                                         Duration duration,
                                         core::Instant now);

    const core::LessonId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Tutor() const noexcept {
        return tutor_;
    }
    const core::PersonId& Student() const noexcept {
        return student_;
    }
    core::Instant StartsAt() const noexcept {
        return starts_at_;
    }
    Duration LessonDuration() const noexcept {
        return duration_;
    }

    core::Instant EndsAt() const noexcept {
        return starts_at_ + duration_;
    }

private:
    Lesson(core::LessonId id,
           core::TenantId tenant,
           core::PersonId tutor,
           core::PersonId student,
           core::Instant starts_at,
           Duration duration);

    core::LessonId id_;
    core::TenantId tenant_;
    core::PersonId tutor_;
    core::PersonId student_;
    core::Instant starts_at_;
    Duration duration_;
};

}  // namespace pdr::scheduling
