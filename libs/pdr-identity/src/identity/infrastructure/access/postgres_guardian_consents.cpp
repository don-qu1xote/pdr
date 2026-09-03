#include "identity/infrastructure/access/postgres_guardian_consents.hpp"

#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Filled;
using infrastructure::db::Timestamptz;

GuardianConsent From(const IdentityGuardianConsentActiveRow& row,
                     const core::TenantId& tenant,
                     const core::PersonId& guardian,
                     const core::PersonId& student) {
    const auto id = ConsentId::Parse(Filled(row.id, "id"));
    const auto scope = ParseGuardianScope(Filled(row.scope, "scope"));
    const auto basis = ParseConsentBasis(Filled(row.basis, "basis"));
    const auto granted_by = core::PersonId::Parse(Filled(row.granted_by, "granted_by"));
    if (!id.has_value() || !scope.has_value() || !basis.has_value() || !granted_by.has_value()) {
        throw std::runtime_error{"identity_guardian_consent: строка не разбирается"};
    }

    std::optional<core::Instant> expires;
    if (row.expires_at.has_value()) {
        expires = AsInstant(*row.expires_at);
    }

    return GuardianConsent::Restore(*id,
                                    tenant,
                                    guardian,
                                    student,
                                    *scope,
                                    *basis,
                                    *granted_by,
                                    AsInstant(Filled(row.granted_at, "granted_at")),
                                    expires,
                                    std::nullopt,
                                    std::nullopt);
}

}  // namespace

PostgresGuardianConsents::PostgresGuardianConsents(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::vector<GuardianConsent> PostgresGuardianConsents::ActiveFor(
    const core::TenantId& tenant,
    const core::PersonId& guardian,
    const core::PersonId& student) const {
    const auto result =
        scope_.Session().Execute(sql::kIdentityGuardianConsentActive, guardian, student);

    std::vector<GuardianConsent> found;
    found.reserve(result.Size());
    for (const auto& row :
         result.AsSetOf<IdentityGuardianConsentActiveRow>(userver::storages::postgres::kRowTag)) {
        found.push_back(From(row, tenant, guardian, student));
    }
    return found;
}

std::optional<GuardianConsent> PostgresGuardianConsents::FindActive(const core::TenantId& tenant,
                                                                    const core::PersonId& guardian,
                                                                    const core::PersonId& student,
                                                                    GuardianScope scope) const {
    for (const auto& consent : ActiveFor(tenant, guardian, student)) {
        if (consent.Scope() == scope) {
            return consent;
        }
    }
    return std::nullopt;
}

void PostgresGuardianConsents::Save(const GuardianConsent& consent) {
    scope_.Session().Execute(sql::kIdentityGuardianConsentSave,
                             consent.Tenant(),
                             consent.Id(),
                             consent.Guardian(),
                             consent.Student(),
                             Name(consent.Scope()),
                             Name(consent.Basis()),
                             consent.GrantedAt(),
                             consent.GrantedBy(),
                             consent.ExpiresAt(),
                             consent.RevokedAt(),
                             consent.RevokedBy());
}

}  // namespace pdr::identity
