#include "identity/application/policies/capability_policy.hpp"

namespace pdr::identity::policies {
namespace {

StudentRule Grown(Capability capability) noexcept {
    return StudentRule{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}, Able{capability}};
}

}  // namespace

StudentRule StudentMovingOwnSlots() noexcept {
    return Grown(Capability::kMoveOwnSlots);
}

StudentRule StudentWritingReview() noexcept {
    return Grown(Capability::kWriteReview);
}

StudentRule StudentDecidingGuardianAccess() noexcept {
    return Grown(Capability::kDecideOwnGuardianAccess);
}

StudentRule StudentPayingOwnMoney() noexcept {
    return Grown(Capability::kPayOwnMoney);
}

StudentRule StudentChoosingTutor() noexcept {
    return Grown(Capability::kChooseTutor);
}

}  // namespace pdr::identity::policies
