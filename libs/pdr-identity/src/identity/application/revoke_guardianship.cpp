#include "identity/application/revoke_guardianship.hpp"

#include "events/identity/guardianship_revoked.hpp"

namespace pdr::identity {

RevokeGuardianship::RevokeGuardianship(ports::GuardianshipRepository& guardianships,
                                       const application::ports::Clock& clock,
                                       events::Bus& bus) noexcept
    : guardianships_{guardianships}, clock_{clock}, bus_{bus} {}

core::Result<void> RevokeGuardianship::Execute(const Request& request) const {
    const auto found = guardianships_.FindActive(request.tenant, request.guardian, request.student);
    if (!found.has_value()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "guardianship_not_found",
                           "действующей опеки между этими людьми нет"};
    }

    const auto now = clock_.Now();
    const auto revoked = found->Revoked(now);
    if (!revoked.HasValue()) {
        return revoked.Failure();
    }

    guardianships_.Save(revoked.Value());

    bus_.Publish(pdr::events::identity::GuardianshipRevoked{
        pdr::events::Envelope{request.tenant, now},
        request.guardian,
        request.student,
    });

    return {};
}

}  // namespace pdr::identity
