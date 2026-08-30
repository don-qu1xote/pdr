#include "identity/core/guardian_consent.hpp"

namespace pdr::identity {

std::string_view Name(ConsentBasis basis) noexcept {
    switch (basis) {
        case ConsentBasis::kGuardianship:
            return "guardianship";
        case ConsentBasis::kNamedByStudent:
            return "named_by_student";
        case ConsentBasis::kPaysForLessons:
            return "pays_for_lessons";
        case ConsentBasis::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<ConsentBasis> ParseConsentBasis(std::string_view text) {
    for (const auto basis : kEveryConsentBasis) {
        if (Name(basis) == text) {
            return basis;
        }
    }
    return std::nullopt;
}

core::Result<GuardianConsent> GuardianConsent::Grant(ConsentId id,
                                                     core::TenantId tenant,
                                                     core::PersonId guardian,
                                                     core::PersonId student,
                                                     GuardianScope scope,
                                                     ConsentBasis basis,
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
    if (basis == ConsentBasis::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_basis_unknown",
                           "такого основания для доступа нет"};
    }
    if (!MayCarry(basis, scope)) {
        return core::Error{core::ErrorKind::kForbidden,
                           "consent_basis_forbids_scope",
                           "плательщик получает деньги и только деньги: смотреть занятия "
                           "оплата не позволяет"};
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
                           basis,
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
                                         ConsentBasis basis,
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
                           basis,
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
                           basis_,
                           granted_by_,
                           granted_at_,
                           expires_at_,
                           at,
                           std::move(by)};
}

}  // namespace pdr::identity
