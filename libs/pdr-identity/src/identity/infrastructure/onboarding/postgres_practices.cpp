#include "identity/infrastructure/onboarding/postgres_practices.hpp"

#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Filled;
using infrastructure::db::Timestamptz;

Practice Parse(const IdentityTenantVisibilityRow& row) {
    const auto tenant = core::TenantId::Parse(Filled(row.tenant_id, "tenant_id"));
    const auto visibility = ParseVisibility(Filled(row.visibility, "visibility"));
    if (!tenant.has_value() || !visibility.has_value()) {
        throw std::runtime_error{"identity_tenant: строка видимости не разбирается"};
    }

    std::optional<core::Instant> asked;
    if (row.visibility_asked_at.has_value()) {
        asked = AsInstant(*row.visibility_asked_at);
    }

    std::optional<core::Instant> decided;
    if (row.visibility_decided_at.has_value()) {
        decided = AsInstant(*row.visibility_decided_at);
    }

    std::optional<RefusalReason> refusal;
    if (row.visibility_refusal.has_value()) {
        refusal = ParseRefusalReason(*row.visibility_refusal);
        if (!refusal.has_value()) {
            throw std::runtime_error{"identity_tenant.visibility_refusal не причина"};
        }
    }

    return Practice::Restore(*tenant,
                             *visibility,
                             AsInstant(Filled(row.created_at, "created_at")),
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
    const auto added = scope_.Session().Execute(sql::kIdentityTenantOpen,
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
    const auto result = scope_.Session().Execute(sql::kIdentityTenantVisibility, tenant.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }
    return Parse(
        result.Front().As<IdentityTenantVisibilityRow>(userver::storages::postgres::kRowTag));
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

    scope_.Session().Execute(sql::kIdentityTenantVisibilitySave,
                             practice.Tenant().ToString(),
                             std::string{Name(practice.Visible())},
                             asked,
                             decided,
                             refusal);
}

}  // namespace pdr::identity
