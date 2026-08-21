#include "jobs/infrastructure/postgres_job_ledger.hpp"

#include <userver/storages/postgres/query.hpp>

namespace pdr::jobs {
namespace {

/// Арендатор в значениях не повторяется параметром: он уже объявлен сессии, и
/// берётся оттуда же, откуда его берёт политика. Два источника одного значения
/// разъезжаются, и разъезд выглядит как «строка вставилась, а потом пропала».
const userver::storages::postgres::Query kClaimEffect{
    "INSERT INTO jobs_effect (tenant_id, job, effect_key) "
    "VALUES (nullif(current_setting('pdr.tenant_id', true), '')::uuid, $1, $2) "
    "ON CONFLICT DO NOTHING "
    "RETURNING 1",
    userver::storages::postgres::Query::Name{"jobs_claim_effect"},
};

}  // namespace

PostgresJobLedger::PostgresJobLedger(Storage& storage) noexcept : storage_{storage} {}

bool PostgresJobLedger::Claim(const core::TenantId& tenant,
                              const JobName& job,
                              const std::string& effect_key) {
    return storage_.InTenant(tenant, [&](userver::storages::postgres::Transaction& transaction) {
        return !transaction.Execute(kClaimEffect, job.Value(), effect_key).IsEmpty();
    });
}

}  // namespace pdr::jobs
