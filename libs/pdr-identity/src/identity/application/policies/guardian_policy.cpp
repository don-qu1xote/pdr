#include "identity/application/policies/guardian_policy.hpp"

#include <array>

namespace pdr::identity::policies {
namespace {

GuardianRule Within(GuardianScope scope) noexcept {
    return GuardianRule{HasRole{Role::kGuardian}, Tied{Tie::kMyWard}, HasScope{scope}};
}

const AnyOf kMayDecideForWhom{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}},
                              AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}}};

constexpr std::array kConsentActions{Action::kManageGuardianAccess};

}  // namespace

GuardianRule GuardianInSchedule() noexcept {
    return Within(GuardianScope::kSchedule);
}

GuardianRule GuardianInPayments() noexcept {
    return Within(GuardianScope::kPayments);
}

GuardianRule GuardianInNotes() noexcept {
    return Within(GuardianScope::kNotesAndHomework);
}

GuardianRule GuardianInRecordings() noexcept {
    return Within(GuardianScope::kRecordings);
}

std::span<const Action> ConsentPolicy::Actions() noexcept {
    return kConsentActions;
}

PolicyDecision ConsentPolicy::Decide(const Subject& subject,
                                     Action action,
                                     const Resource& resource) const {
    switch (action) {
        case Action::kManageGuardianAccess:
            return kMayDecideForWhom.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
