#include "identity/application/policies/journal_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kMayRead{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}},
                     AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}};

constexpr std::array kActions{Action::kViewAccessJournal};

}  // namespace

std::span<const Action> JournalPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision JournalPolicy::Decide(const Subject& subject,
                                     Action action,
                                     const Resource& resource) const {
    switch (action) {
        case Action::kViewAccessJournal:
            return kMayRead.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
