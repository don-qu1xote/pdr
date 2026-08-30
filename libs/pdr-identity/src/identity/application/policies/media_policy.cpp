#include "identity/application/policies/media_policy.hpp"

#include <array>

#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

const AnyOf kMayListen{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}},
                       AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}},
                       GuardianInRecordings()};

constexpr std::array kActions{
    Action::kViewLessonRecording,
    Action::kViewLessonTranscript,
};

}  // namespace

std::span<const Action> MediaPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision MediaPolicy::Decide(const Subject& subject,
                                   Action action,
                                   const Resource& resource) const {
    switch (action) {
        case Action::kViewLessonRecording:
        case Action::kViewLessonTranscript:
            return kMayListen.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
