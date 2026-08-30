#include "identity/application/policies/review_policy.hpp"

#include <array>

#include "identity/application/policies/capability_policy.hpp"

namespace pdr::identity::policies {
namespace {

const StudentRule kAuthor = StudentWritingReview();

constexpr std::array kActions{Action::kWriteReview};

}  // namespace

std::span<const Action> ReviewPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision ReviewPolicy::Decide(const Subject& subject,
                                    Action action,
                                    const Resource& resource) const {
    switch (action) {
        case Action::kWriteReview:
            return kAuthor.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
