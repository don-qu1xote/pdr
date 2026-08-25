#pragma once

#include <optional>

#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling::ports {

/// Узкий порт: занято ли время у репетитора и как сохранить назначенное.
class LessonRepository {
public:
    LessonRepository(const LessonRepository&) = delete;
    LessonRepository& operator=(const LessonRepository&) = delete;

    virtual ~LessonRepository() = default;

    virtual std::optional<Lesson> FindAtSlot(const core::TenantId& tenant,
                                             const core::PersonId& tutor,
                                             core::Instant starts_at) const = 0;

    virtual void Save(const Lesson& lesson) = 0;

protected:
    LessonRepository() = default;
};

}  // namespace pdr::scheduling::ports
