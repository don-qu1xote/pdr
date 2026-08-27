#include "identity/application/policies/subject_builder.hpp"

#include <optional>

namespace pdr::identity::policies {

SubjectBuilder::SubjectBuilder(const ports::GuardianshipRepository& guardianships,
                               const ports::RoleRepository& roles,
                               const ports::GuardianConsents& consents,
                               const ports::BirthDates& birth_dates,
                               const ports::MaturitySettings& maturity,
                               const application::ports::Clock& clock) noexcept
    : guardianships_{guardianships},
      roles_{roles},
      consents_{consents},
      birth_dates_{birth_dates},
      maturity_{maturity},
      clock_{clock} {}

bool SubjectBuilder::Guards(const core::TenantId& tenant,
                            const core::PersonId& actor,
                            const Resource& resource) const {
    if (!resource.subject.has_value() || *resource.subject == actor) {
        return false;
    }
    return guardianships_.FindActive(tenant, actor, *resource.subject).has_value();
}

Tie SubjectBuilder::TieFor(const core::TenantId& tenant,
                           const core::PersonId& actor,
                           const Resource& resource) const {
    return TieBetween(actor, resource, Guards(tenant, actor, resource));
}

GuardianAccess SubjectBuilder::AccessOf(const core::TenantId& tenant,
                                        const core::PersonId& guardian,
                                        const core::PersonId& student) const {
    const auto rule = maturity_.Rule();
    if (!rule) {
        return GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}};
    }

    const auto consents = consents_.ActiveFor(tenant, guardian, student);
    return WeighConsents(consents, birth_dates_.Of(tenant, student), rule.Value(), clock_.Now());
}

Subject SubjectBuilder::For(const core::TenantId& tenant,
                            const core::PersonId& actor,
                            const Resource& resource) const {
    const auto tie = TieFor(tenant, actor, resource);

    auto access = GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}};
    if (tie == Tie::kMyWard && resource.subject.has_value()) {
        access = AccessOf(tenant, actor, *resource.subject);
    }

    return Subject{tenant, actor, roles_.RolesOf(tenant, actor), tie, access};
}

}  // namespace pdr::identity::policies
