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

core::Result<Guardianship> Guardianship::Establish(const TenantMembership& guardian,
                                                   const TenantMembership& student,
                                                   core::Instant granted_at) {
    if (!guardian.SameTenantAs(student)) {
        return core::Error{core::ErrorKind::kValidation,
                           "guardianship_across_tenants",
                           "опекун и ученик числятся у разных репетиторов"};
    }
    if (guardian.InRole() != Role::kGuardian) {
        return core::Error{core::ErrorKind::kValidation,
                           "guardianship_guardian_role_missing",
                           "опеку выдают тому, кто здесь опекун"};
    }
    if (student.InRole() != Role::kStudent) {
        return core::Error{core::ErrorKind::kValidation,
                           "guardianship_student_role_missing",
                           "опеку выдают над тем, кто здесь ученик"};
    }

    return Grant(guardian.TenantId(), guardian.Person(), student.Person(), granted_at);
}

core::Result<Guardianship> Guardianship::Grant(core::TenantId tenant,
                                               core::PersonId guardian,
                                               core::PersonId student,
                                               core::Instant granted_at) {
    if (guardian == student) {
        return core::Error{core::ErrorKind::kValidation,
                           "guardianship_self",
                           "ученик не может быть опекуном самому себе"};
    }

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
