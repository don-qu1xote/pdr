#pragma once

#include <optional>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Занятия в Postgres.
///
/// Строится от ОБЛАСТИ АРЕНДАТОРА, а не от пула: пул в этом заголовке не
/// упоминается вовсе, и «сходить в базу мимо арендатора» здесь нечем написать
/// (`scripts/check_layers.py`).
class PostgresLessonRepository final : public ports::LessonRepository {
public:
    explicit PostgresLessonRepository(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::optional<Lesson> Find(const core::TenantId& tenant,
                               const core::LessonId& id) const override;

    std::optional<Lesson> FindAtSlot(const core::TenantId& tenant,
                                     const core::PersonId& tutor,
                                     core::Instant starts_at) const override;

    std::vector<Lesson> OfTutor(const core::TenantId& tenant,
                                const core::PersonId& tutor,
                                const core::TimeRange& window) const override;

    std::vector<Lesson> OfParticipant(const core::TenantId& tenant,
                                      const core::PersonId& participant,
                                      const core::TimeRange& window) const override;

    core::Result<void> Save(const Lesson& lesson) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
