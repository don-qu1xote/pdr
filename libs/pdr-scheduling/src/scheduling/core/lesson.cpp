#include "scheduling/core/lesson.hpp"

#include <utility>

namespace pdr::scheduling {

Lesson::Lesson(core::LessonId id,
               core::TenantId tenant,
               core::PersonId tutor,
               core::PersonId student,
               core::Instant starts_at,
               Duration duration)
    : id_{std::move(id)},
      tenant_{std::move(tenant)},
      tutor_{std::move(tutor)},
      student_{std::move(student)},
      starts_at_{starts_at},
      duration_{duration} {}

core::Result<Lesson> Lesson::Schedule(core::LessonId id,
                                      core::TenantId tenant,
                                      core::PersonId tutor,
                                      core::PersonId student,
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

    return Lesson{std::move(id),
                  std::move(tenant),
                  std::move(tutor),
                  std::move(student),
                  starts_at,
                  duration};
}

}  // namespace pdr::scheduling
