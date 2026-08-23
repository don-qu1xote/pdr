#include "identity/application/contract_service.hpp"

namespace pdr::identity {

ContractService::ContractService(const ports::GuardianshipRepository& guardianships) noexcept
    : guardianships_{guardianships} {}

bool ContractService::MayActFor(const core::TenantId& tenant,
                                const core::PersonId& actor,
                                const core::PersonId& student) const {
    if (actor == student) {
        return true;
    }

    return guardianships_.FindActive(tenant, actor, student).has_value();
}

}  // namespace pdr::identity
