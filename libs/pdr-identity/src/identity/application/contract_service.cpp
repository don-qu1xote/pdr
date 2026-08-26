#include "identity/application/contract_service.hpp"

#include <optional>

#include "identity/application/policies/subject.hpp"

namespace pdr::identity {

ContractService::ContractService(const ports::GuardianshipRepository& guardianships,
                                 const ports::RoleRepository& roles,
                                 const policies::PolicySet& permissions) noexcept
    : guardianships_{guardianships}, roles_{roles}, permissions_{permissions} {}

bool ContractService::Guards(const core::TenantId& tenant,
                             const core::PersonId& actor,
                             const Resource& resource) const {
    if (!resource.subject.has_value() || *resource.subject == actor) {
        return false;
    }
    return guardianships_.FindActive(tenant, actor, *resource.subject).has_value();
}

bool ContractService::MayActFor(const core::TenantId& tenant,
                                const core::PersonId& actor,
                                const core::PersonId& student) const {
    const Resource about{tenant, std::nullopt, student};
    const auto tie = TieBetween(actor, about, Guards(tenant, actor, about));

    return tie == Tie::kAboutMe || tie == Tie::kMyWard;
}

PolicyDecision ContractService::Decide(const core::TenantId& tenant,
                                       const core::PersonId& actor,
                                       Action action,
                                       const Resource& resource) const {
    const Subject subject{tenant,
                          actor,
                          roles_.RolesOf(tenant, actor),
                          TieBetween(actor, resource, Guards(tenant, actor, resource))};

    return permissions_.Decide(subject, action, resource);
}

}  // namespace pdr::identity
