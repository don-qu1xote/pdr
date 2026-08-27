#include "identity/application/policies/scheduling_policy.hpp"

#include <array>

#include "identity/application/policies/capability_policy.hpp"
#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kMayBook{
    AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, StudentChoosingTutor(), GuardianInSchedule()};

const AnyOf kMayMove{
    AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, StudentMovingOwnSlots(), GuardianInSchedule()};

const AnyOf kOwnAffairs{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}},
                        AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}},
                        GuardianInSchedule()};

const AnyOf kMayLook{kOwnAffairs, HasRole{Role::kOwner}};

constexpr std::array kActions{
    Action::kBookLesson,
    Action::kCancelLesson,
    Action::kRescheduleLesson,
    Action::kViewSchedule,
};

}  // namespace

std::span<const Action> SchedulingPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision SchedulingPolicy::Decide(const Subject& subject,
                                        Action action,
                                        const Resource& resource) const {
    switch (action) {
        case Action::kBookLesson:
            return kMayBook.Decide(subject, action, resource);
        case Action::kCancelLesson:
        case Action::kRescheduleLesson:
            return kMayMove.Decide(subject, action, resource);
        case Action::kViewSchedule:
            return kMayLook.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
