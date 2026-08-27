#include "identity/application/revoke_guardian_scope.hpp"

namespace pdr::identity {

RevokeGuardianScope::RevokeGuardianScope(ports::GuardianConsents& consents,
                                         const application::ports::Clock& clock) noexcept
    : consents_{consents}, clock_{clock} {}

core::Result<void> RevokeGuardianScope::Execute(const RevokeGuardianScopeRequest& request) const {
    const auto found =
        consents_.FindActive(request.tenant, request.guardian, request.student, request.scope);
    if (!found.has_value()) {
        return core::Error{
            core::ErrorKind::kNotFound, "consent_not_found", "этот уровень доступа и так закрыт"};
    }

    const auto revoked = found->Revoked(clock_.Now(), request.revoked_by);
    if (!revoked) {
        return revoked.Failure();
    }

    consents_.Save(revoked.Value());
    return {};
}

}  // namespace pdr::identity
