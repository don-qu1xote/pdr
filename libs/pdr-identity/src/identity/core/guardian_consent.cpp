#include "identity/core/guardian_consent.hpp"

namespace pdr::identity {

core::Result<GuardianConsent> GuardianConsent::Grant(ConsentId id,
                                                     core::TenantId tenant,
                                                     core::PersonId guardian,
                                                     core::PersonId student,
                                                     GuardianScope scope,
                                                     core::PersonId granted_by,
                                                     core::Instant granted_at,
                                                     std::optional<core::Instant> expires_at) {
    if (guardian == student) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_self_guardianship",
                           "ученик не бывает опекуном самому себе"};
    }
    if (scope == GuardianScope::kBoundary) {
        return core::Error{
            core::ErrorKind::kValidation, "consent_scope_unknown", "такого уровня доступа нет"};
    }
    if (expires_at.has_value() && *expires_at <= granted_at) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_expires_before_granted",
                           "согласие, истёкшее в момент выдачи, ничего не открывает"};
    }

    return GuardianConsent{std::move(id),
                           std::move(tenant),
                           std::move(guardian),
                           std::move(student),
                           scope,
                           std::move(granted_by),
                           granted_at,
                           expires_at,
                           std::nullopt,
                           std::nullopt};
}

GuardianConsent GuardianConsent::Restore(ConsentId id,
                                         core::TenantId tenant,
                                         core::PersonId guardian,
                                         core::PersonId student,
                                         GuardianScope scope,
                                         core::PersonId granted_by,
                                         core::Instant granted_at,
                                         std::optional<core::Instant> expires_at,
                                         std::optional<core::Instant> revoked_at,
                                         std::optional<core::PersonId> revoked_by) {
    return GuardianConsent{std::move(id),
                           std::move(tenant),
                           std::move(guardian),
                           std::move(student),
                           scope,
                           std::move(granted_by),
                           granted_at,
                           expires_at,
                           revoked_at,
                           std::move(revoked_by)};
}

bool GuardianConsent::IsActiveAt(core::Instant moment) const noexcept {
    if (revoked_at_.has_value()) {
        return false;
    }
    if (expires_at_.has_value() && moment >= *expires_at_) {
        return false;
    }
    return moment >= granted_at_;
}

core::Result<GuardianConsent> GuardianConsent::Revoked(core::Instant at, core::PersonId by) const {
    if (revoked_at_.has_value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "consent_already_revoked",
                           "этот уровень доступа уже отозван"};
    }
    if (at < granted_at_) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_revoked_before_granted",
                           "отзыв стоит раньше выдачи"};
    }

    return GuardianConsent{id_,
                           tenant_,
                           guardian_,
                           student_,
                           scope_,
                           granted_by_,
                           granted_at_,
                           expires_at_,
                           at,
                           std::move(by)};
}

}  // namespace pdr::identity
