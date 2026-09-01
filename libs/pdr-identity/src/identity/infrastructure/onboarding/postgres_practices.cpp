#include "identity/infrastructure/onboarding/postgres_practices.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

const userver::storages::postgres::Query kOpen{
    "INSERT INTO identity_tenant (tenant_id, name, tz, visibility, created_at) "
    "VALUES ($1::uuid, $2, $3, $4, $5) ON CONFLICT (tenant_id) DO NOTHING",
    userver::storages::postgres::Query::Name{"identity_tenant_open"},
};

const userver::storages::postgres::Query kFind{
    "SELECT tenant_id::text AS tenant_id, visibility, created_at, visibility_asked_at, "
    "visibility_decided_at, "
    "visibility_refusal FROM identity_tenant WHERE tenant_id = $1::uuid",
    userver::storages::postgres::Query::Name{"identity_tenant_visibility"},
};

const userver::storages::postgres::Query kSave{
    "UPDATE identity_tenant SET visibility = $2, visibility_asked_at = $3, "
    "visibility_decided_at = $4, visibility_refusal = $5 WHERE tenant_id = $1::uuid",
    userver::storages::postgres::Query::Name{"identity_tenant_visibility_save"},
};

Practice Parse(const userver::storages::postgres::Row& row) {
    const auto tenant = core::TenantId::Parse(row["tenant_id"].As<std::string>());
    const auto visibility = ParseVisibility(row["visibility"].As<std::string>());
    if (!tenant.has_value() || !visibility.has_value()) {
        throw std::runtime_error{"identity_tenant: строка видимости не разбирается"};
    }

    std::optional<core::Instant> asked;
    const auto stored_asked = row["visibility_asked_at"].As<std::optional<Timestamptz>>();
    if (stored_asked.has_value()) {
        asked = AsInstant(*stored_asked);
    }

    std::optional<core::Instant> decided;
    const auto stored_decided = row["visibility_decided_at"].As<std::optional<Timestamptz>>();
    if (stored_decided.has_value()) {
        decided = AsInstant(*stored_decided);
    }

    std::optional<RefusalReason> refusal;
    const auto stored_refusal = row["visibility_refusal"].As<std::optional<std::string>>();
    if (stored_refusal.has_value()) {
        refusal = ParseRefusalReason(*stored_refusal);
        if (!refusal.has_value()) {
            throw std::runtime_error{"identity_tenant.visibility_refusal не причина"};
        }
    }

    return Practice::Restore(*tenant,
                             *visibility,
                             AsInstant(row["created_at"].As<Timestamptz>()),
                             asked,
                             decided,
                             refusal);
}

}  // namespace

PostgresPractices::PostgresPractices(infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

core::Result<void> PostgresPractices::Open(const Tenant& tenant,
                                           const core::TimeZone& zone,
                                           const Practice& practice) {
    const auto added = scope_.Session().Execute(kOpen,
                                                tenant.Id().ToString(),
                                                tenant.Name(),
                                                zone.Name(),
                                                std::string{Name(practice.Visible())},
                                                AsTimestamptz(practice.OpenedAt()));
    if (added.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kConflict,
                           "practice_already_open",
                           "практика с таким идентификатором уже есть"};
    }
    return {};
}

std::optional<Practice> PostgresPractices::Find(const core::TenantId& tenant) const {
    const auto result = scope_.Session().Execute(kFind, tenant.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }
    return Parse(result.Front());
}

void PostgresPractices::Save(const Practice& practice) {
    std::optional<Timestamptz> asked;
    if (practice.AskedAt().has_value()) {
        asked = AsTimestamptz(*practice.AskedAt());
    }

    std::optional<Timestamptz> decided;
    if (practice.DecidedAt().has_value()) {
        decided = AsTimestamptz(*practice.DecidedAt());
    }

    std::optional<std::string> refusal;
    if (practice.Refusal().has_value()) {
        refusal = std::string{Name(*practice.Refusal())};
    }

    scope_.Session().Execute(kSave,
                             practice.Tenant().ToString(),
                             std::string{Name(practice.Visible())},
                             asked,
                             decided,
                             refusal);
}

}  // namespace pdr::identity
