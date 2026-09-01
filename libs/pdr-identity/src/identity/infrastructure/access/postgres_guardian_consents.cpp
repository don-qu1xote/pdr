#include "identity/infrastructure/access/postgres_guardian_consents.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

const userver::storages::postgres::Query kActiveFor{
    "SELECT id::text AS id, scope, basis, granted_by::text AS granted_by, granted_at, "
    "expires_at "
    "FROM identity_guardian_consent "
    "WHERE guardian_id = $1::uuid AND student_id = $2::uuid AND revoked_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_guardian_consent_active"},
};

/// Отзыв — правка строки, а не новая рядом: частичный уникальный индекс не
/// позволил бы двум действующим согласиям на пару и уровень, и правильно.
const userver::storages::postgres::Query kSave{
    "INSERT INTO identity_guardian_consent "
    "(tenant_id, id, guardian_id, student_id, scope, basis, granted_at, granted_by, "
    "expires_at, revoked_at, revoked_by) "
    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5, $6, $7, $8::uuid, $9, $10, $11::uuid) "
    "ON CONFLICT (tenant_id, id) DO UPDATE "
    "SET revoked_at = excluded.revoked_at, revoked_by = excluded.revoked_by",
    userver::storages::postgres::Query::Name{"identity_guardian_consent_save"},
};

GuardianConsent From(const userver::storages::postgres::Row& row,
                     const core::TenantId& tenant,
                     const core::PersonId& guardian,
                     const core::PersonId& student) {
    const auto id = ConsentId::Parse(row["id"].As<std::string>());
    const auto scope = ParseGuardianScope(row["scope"].As<std::string>());
    const auto basis = ParseConsentBasis(row["basis"].As<std::string>());
    const auto granted_by = core::PersonId::Parse(row["granted_by"].As<std::string>());
    if (!id.has_value() || !scope.has_value() || !basis.has_value() || !granted_by.has_value()) {
        throw std::runtime_error{"identity_guardian_consent: строка не разбирается"};
    }

    std::optional<core::Instant> expires;
    const auto stored = row["expires_at"].As<std::optional<Timestamptz>>();
    if (stored.has_value()) {
        expires = AsInstant(*stored);
    }

    return GuardianConsent::Restore(*id,
                                    tenant,
                                    guardian,
                                    student,
                                    *scope,
                                    *basis,
                                    *granted_by,
                                    AsInstant(row["granted_at"].As<Timestamptz>()),
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
        scope_.Session().Execute(kActiveFor, guardian.ToString(), student.ToString());

    std::vector<GuardianConsent> found;
    found.reserve(result.Size());
    for (const auto& row : result) {
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
    std::optional<Timestamptz> expires;
    if (consent.ExpiresAt().has_value()) {
        expires = AsTimestamptz(*consent.ExpiresAt());
    }
    std::optional<Timestamptz> revoked;
    if (consent.RevokedAt().has_value()) {
        revoked = AsTimestamptz(*consent.RevokedAt());
    }
    std::optional<std::string> revoked_by;
    if (consent.RevokedBy().has_value()) {
        revoked_by = consent.RevokedBy()->ToString();
    }

    scope_.Session().Execute(kSave,
                             consent.Tenant().ToString(),
                             consent.Id().ToString(),
                             consent.Guardian().ToString(),
                             consent.Student().ToString(),
                             std::string{Name(consent.Scope())},
                             std::string{Name(consent.Basis())},
                             AsTimestamptz(consent.GrantedAt()),
                             consent.GrantedBy().ToString(),
                             expires,
                             revoked,
                             revoked_by);
}

}  // namespace pdr::identity
