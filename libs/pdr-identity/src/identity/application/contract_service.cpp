#include "identity/application/contract_service.hpp"

#include <optional>

namespace pdr::identity {

ContractService::ContractService(const policies::SubjectBuilder& subjects,
                                 const policies::PolicySet& permissions,
                                 const NoteSensitiveAccess& journal) noexcept
    : subjects_{subjects}, permissions_{permissions}, journal_{journal} {}

bool ContractService::MayActFor(const core::TenantId& tenant,
                                const core::PersonId& actor,
                                const core::PersonId& student) const {
    const Resource about{tenant, std::nullopt, student};
    const auto tie = subjects_.TieFor(tenant, actor, about);

    return tie == Tie::kAboutMe || tie == Tie::kMyWard;
}

PolicyDecision ContractService::Decide(const core::TenantId& tenant,
                                       const core::PersonId& actor,
                                       Action action,
                                       const Resource& resource) const {
    const auto decision =
        permissions_.Decide(subjects_.For(tenant, actor, resource), action, resource);

    const auto kind = JournalledKind(action);
    if (kind.has_value() && resource.subject.has_value() && *resource.subject != actor) {
        static_cast<void>(
            journal_.Execute(tenant,
                             actor,
                             *resource.subject,
                             *kind,
                             decision.allowed ? AccessOutcome::kShown : AccessOutcome::kRefused));
    }

    return decision;
}

}  // namespace pdr::identity
