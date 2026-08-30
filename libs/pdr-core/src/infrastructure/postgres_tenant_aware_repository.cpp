#include "infrastructure/postgres_tenant_aware_repository.hpp"

namespace pdr::infrastructure {

PostgresTenantAwareRepository::PostgresTenantAwareRepository(db::TenantContext& context) noexcept
    : context_{context} {}

void PostgresTenantAwareRepository::Run(const core::TenantId& tenant, const Work& work) {
    auto scope = context_.Open(tenant);

    work(scope.Session());

    scope.Commit();
}

}  // namespace pdr::infrastructure
