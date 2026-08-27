#include "identity/application/policies/progress_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kOwnData{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}}, GuardianInNotes()};

const AnyOf kWatchers{kOwnData, AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}};

const AllOf kSolver{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}};

const HasRole kSchoolWide{Role::kOwner};

constexpr std::array kActions{
    Action::kViewProgress,
    Action::kRecordAttempt,
    Action::kExportProgress,
    Action::kViewTenantProgress,
};

}  // namespace

std::span<const Action> ProgressPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision ProgressPolicy::Decide(const Subject& subject,
                                      Action action,
                                      const Resource& resource) const {
    switch (action) {
        case Action::kViewProgress:
            return kWatchers.Decide(subject, action, resource);
        case Action::kRecordAttempt:
            return kSolver.Decide(subject, action, resource);
        case Action::kExportProgress:
            return kOwnData.Decide(subject, action, resource);
        case Action::kViewTenantProgress:
            return kSchoolWide.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
