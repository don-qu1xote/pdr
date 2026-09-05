#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"

namespace pdr::scheduling {

/// Серии занятий в Postgres.
///
/// Хранится ПРАВИЛО: строка rrule, дата начала, минуты от полуночи и зона.
/// Развёрнутого списка занятий в базе нет ни одной строкой — их считает
/// `Expand` (PDR-SCHED-02).
class PostgresRecurrenceRepository final : public ports::RecurrenceRepository {
public:
    explicit PostgresRecurrenceRepository(infrastructure::db::ScopedTenantContext& scope) noexcept;

    core::Result<void> Create(const RecurrenceSeries& series) override;

    std::optional<RecurrenceSeries> Find(const core::TenantId& tenant,
                                         const core::SeriesId& id) const override;

    core::Result<void> Record(const core::TenantId& tenant,
                              const core::SeriesId& id,
                              const RecurrenceException& exception) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
