#include "identity/application/policies/billing_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kPayer{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}},
                   AllOf{HasRole{Role::kGuardian}, Tied{Tie::kMyWard}}};

const AnyOf kSeller{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, HasRole{Role::kOwner}};

const AnyOf kBothSides{kPayer, kSeller};

constexpr std::array kActions{
    Action::kViewInvoice,
    Action::kPayInvoice,
    Action::kIssueRefund,
    Action::kSetTariff,
};

}  // namespace

std::span<const Action> BillingPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision BillingPolicy::Decide(const Subject& subject,
                                     Action action,
                                     const Resource& resource) const {
    switch (action) {
        case Action::kViewInvoice:
            return kBothSides.Decide(subject, action, resource);
        case Action::kPayInvoice:
            return kPayer.Decide(subject, action, resource);
        case Action::kIssueRefund:
        case Action::kSetTariff:
            return kSeller.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
