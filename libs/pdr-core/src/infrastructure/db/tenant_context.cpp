#include "infrastructure/db/tenant_context.hpp"

#include <pdr/sql_queries.hpp>

namespace pdr::infrastructure::db {

TenantContext::TenantContext(userver::storages::postgres::ClusterPtr cluster)
    : cluster_{std::move(cluster)} {}

ScopedTenantContext TenantContext::Open(
    const core::TenantId& tenant,
    userver::storages::postgres::ClusterHostTypeFlags host,
    const userver::storages::postgres::TransactionOptions& options) {
    auto transaction = cluster_->Begin(host, options);
    transaction.Execute(sql::kDeclareTenant, tenant.ToString());

    return ScopedTenantContext{std::move(transaction), tenant};
}

}  // namespace pdr::infrastructure::db
