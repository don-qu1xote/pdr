#pragma once

#include <vector>

#include "application/ports/id_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/lesson_history.hpp"

namespace pdr::scheduling {

/// История занятия в Postgres.
///
/// Живёт внутри области арендатора и не переживает запрос: пул соединений в
/// заголовке не упоминается вовсе (`scripts/check_layers.py`).
class PostgresLessonHistory final : public ports::LessonHistory {
public:
    PostgresLessonHistory(infrastructure::db::ScopedTenantContext& scope,
                          const application::ports::IdGenerator& ids) noexcept;

    core::Result<void> Record(const LessonHistoryEntry& entry) override;

    std::vector<LessonHistoryEntry> Of(const core::TenantId& tenant,
                                       const core::LessonId& lesson) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::scheduling
