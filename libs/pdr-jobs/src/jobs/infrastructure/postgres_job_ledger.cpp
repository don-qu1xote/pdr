#include "jobs/infrastructure/postgres_job_ledger.hpp"

#include <pdr/sql_queries.hpp>

namespace pdr::jobs {
PostgresJobLedger::PostgresJobLedger(Storage& storage) noexcept : storage_{storage} {}

bool PostgresJobLedger::Claim(const core::TenantId& tenant,
                              const JobName& job,
                              const std::string& effect_key) {
    return storage_.InTenant(application::ports::Intent::kChanging,
                             tenant,
                             [&](infrastructure::db::ScopedTenantContext& scope) {
                                 return !scope.Session()
                                             .Execute(
                                                 sql::kJobsClaimEffect, job.Value(), effect_key)
                                             .IsEmpty();
                             });
}

}  // namespace pdr::jobs
