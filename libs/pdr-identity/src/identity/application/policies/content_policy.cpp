#include "identity/application/policies/content_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

const AllOf kAuthor{HasRole{Role::kTutor}, Tied{Tie::kMine}};

const AnyOf kAssigned{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}}, GuardianInNotes()};

const AnyOf kReaders{kAuthor, kAssigned};

constexpr std::array kActions{
    Action::kViewMaterial,
    Action::kEditMaterial,
    Action::kPublishMaterial,
    Action::kAssignPlan,
};

}  // namespace

std::span<const Action> ContentPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision ContentPolicy::Decide(const Subject& subject,
                                     Action action,
                                     const Resource& resource) const {
    switch (action) {
        case Action::kViewMaterial:
            return kReaders.Decide(subject, action, resource);
        case Action::kEditMaterial:
        case Action::kPublishMaterial:
        case Action::kAssignPlan:
            return kAuthor.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
