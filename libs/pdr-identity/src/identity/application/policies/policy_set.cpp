#include "identity/application/policies/policy_set.hpp"

namespace pdr::identity::policies {

PolicySet::PolicySet(const ports::ConfigurationFaults& faults) noexcept : faults_{faults} {
    Cover(scheduling_, SchedulingPolicy::Actions());
    Cover(billing_, BillingPolicy::Actions());
    Cover(content_, ContentPolicy::Actions());
    Cover(progress_, ProgressPolicy::Actions());
    Cover(media_, MediaPolicy::Actions());
    Cover(journal_, JournalPolicy::Actions());
    Cover(consents_, ConsentPolicy::Actions());
}

void PolicySet::Cover(const Policy& policy, std::span<const Action> actions) noexcept {
    for (const auto action : actions) {
        table_[static_cast<std::size_t>(action)] = &policy;
    }
}

bool PolicySet::Covers(Action action) const noexcept {
    if (action == Action::kBoundary) {
        return false;
    }
    return table_[static_cast<std::size_t>(action)] != nullptr;
}

PolicyDecision PolicySet::Decide(const Subject& subject,
                                 Action action,
                                 const Resource& resource) const {
    if (subject.Tenant() != resource.tenant) {
        return Denied(DenyReason::kForeignTenant);
    }

    if (!Covers(action)) {
        faults_.NoPolicyFor(action);
        return Denied(DenyReason::kNoPolicy);
    }

    return table_[static_cast<std::size_t>(action)]->Decide(subject, action, resource);
}

}  // namespace pdr::identity::policies
