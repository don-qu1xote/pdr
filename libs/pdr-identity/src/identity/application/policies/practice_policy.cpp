#include "identity/application/policies/practice_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kMayInvite{HasRole{Role::kOwner}, HasRole{Role::kTutor}};

const HasRole kOwnsIt{Role::kOwner};

constexpr std::array kActions{Action::kInvitePeople, Action::kManagePractice};

}  // namespace

std::span<const Action> PracticePolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision PracticePolicy::Decide(const Subject& subject,
                                      Action action,
                                      const Resource& resource) const {
    switch (action) {
        case Action::kInvitePeople:
            return kMayInvite.Decide(subject, action, resource);
        case Action::kManagePractice:
            return kOwnsIt.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
