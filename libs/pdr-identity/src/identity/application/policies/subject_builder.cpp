#include "identity/application/policies/subject_builder.hpp"

#include <optional>

#include "identity/core/age_status.hpp"

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

bool SubjectBuilder::LooksAfter(const core::TenantId& tenant,
                                const core::PersonId& actor,
                                const Resource& resource) const {
    if (!resource.subject.has_value() || *resource.subject == actor) {
        return false;
    }
    if (guardianships_.FindActive(tenant, actor, *resource.subject).has_value()) {
        return true;
    }

    const auto now = clock_.Now();
    for (const auto& consent : consents_.ActiveFor(tenant, actor, *resource.subject)) {
        if (NamedByTheStudentHimself(consent.Basis()) && consent.IsActiveAt(now)) {
            return true;
        }
    }
    return false;
}

Tie SubjectBuilder::TieFor(const core::TenantId& tenant,
                           const core::PersonId& actor,
                           const Resource& resource) const {
    return TieBetween(actor, resource, LooksAfter(tenant, actor, resource));
}

GuardianAccess SubjectBuilder::AccessOf(const core::TenantId& tenant,
                                        const core::PersonId& guardian,
                                        const core::PersonId& student) const {
    const auto rule = maturity_.Rule();
    if (!rule) {
        return GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}};
    }

    const auto consents = consents_.ActiveFor(tenant, guardian, student);
    const bool guarded = guardianships_.FindActive(tenant, guardian, student).has_value();
    return WeighConsents(
        consents, birth_dates_.Of(tenant, student), rule.Value(), clock_.Now(), guarded);
}

Capabilities SubjectBuilder::AbilityOf(const core::TenantId& tenant,
                                       const core::PersonId& person) const {
    const auto rule = maturity_.Rule();
    const auto born_on = birth_dates_.Of(tenant, person);
    if (!rule || !born_on.has_value()) {
        return Capabilities{};
    }

    const auto age = AgeStatus::At(*born_on, clock_.Now());
    if (!age) {
        return Capabilities{};
    }

    return Compute(age.Value(), rule.Value().Thresholds());
}

Subject SubjectBuilder::For(const core::TenantId& tenant,
                            const core::PersonId& actor,
                            const Resource& resource) const {
    const auto tie = TieFor(tenant, actor, resource);

    auto access = GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}};
    if (tie == Tie::kInMyCare && resource.subject.has_value()) {
        access = AccessOf(tenant, actor, *resource.subject);
    }

    return Subject{
        tenant, actor, roles_.RolesOf(tenant, actor), tie, access, AbilityOf(tenant, actor)};
}

}  // namespace pdr::identity::policies
