#pragma once

#include <string>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "jobs/application/ports/job_ledger.hpp"
#include "jobs/core/job_name.hpp"

namespace pdr::jobs {

/// След действия в базе: одна вставка, и та под арендатором.
///
/// Атомарность здесь не написана, а взята у схемы: первичный ключ
/// `jobs_effect (tenant_id, job, effect_key)`. Двое, спросившие про один ключ
/// одновременно, получают один вставленную строку, второй — конфликт; «сначала
/// select, потом insert» дало бы обоим «ещё не делали» ровно в тот момент, когда
/// это важно.
///
/// Арендатор объявляется базе там же, где всегда — в
/// `pdr::infrastructure::PostgresTenantAwareRepository`. Фоновая работа ходит под
/// политикой RLS так же, как запрос человека: своего пути в обход у неё нет.
class PostgresJobLedger final : public ports::JobLedger {
public:
    using Storage =
        application::ports::TenantAwareRepository<infrastructure::db::ScopedTenantContext>;

    explicit PostgresJobLedger(Storage& storage) noexcept;

    bool Claim(const core::TenantId& tenant,
               const JobName& job,
               const std::string& effect_key) override;

private:
    Storage& storage_;
};

}  // namespace pdr::jobs
