#include "identity/core/guardianship.hpp"

#include <utility>

namespace pdr::identity {

Guardianship::Guardianship(core::TenantId tenant,
                           core::PersonId guardian,
                           core::PersonId student,
                           core::Instant granted_at,
                           std::optional<core::Instant> revoked_at)
    : tenant_{std::move(tenant)},
      guardian_{std::move(guardian)},
      student_{std::move(student)},
      granted_at_{granted_at},
      revoked_at_{revoked_at} {}

Guardianship Guardianship::Grant(core::TenantId tenant,
                                 core::PersonId guardian,
                                 core::PersonId student,
                                 core::Instant granted_at) {
    return Guardianship{
        std::move(tenant), std::move(guardian), std::move(student), granted_at, std::nullopt};
}

Guardianship Guardianship::Restore(core::TenantId tenant,
                                   core::PersonId guardian,
                                   core::PersonId student,
                                   core::Instant granted_at,
                                   std::optional<core::Instant> revoked_at) {
    return Guardianship{
        std::move(tenant), std::move(guardian), std::move(student), granted_at, revoked_at};
}

core::Result<Guardianship> Guardianship::Revoked(core::Instant now) const {
    if (revoked_at_.has_value()) {
        return core::Error{
            core::ErrorKind::kConflict, "guardianship_already_revoked", "опека отозвана раньше"};
    }
    if (now < granted_at_) {
        return core::Error{core::ErrorKind::kValidation,
                           "guardianship_revoked_before_granted",
                           "отзыв не может случиться раньше выдачи"};
    }

    return Guardianship{tenant_, guardian_, student_, granted_at_, now};
}

}  // namespace pdr::identity
