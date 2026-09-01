#include "identity/infrastructure/access/postgres_guardianships.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

/// Идентификатор строки опеки. Метки в общем списке `core/types/ids.hpp` для
/// него нет намеренно: на строку не ссылается никто — домен знает опеку по
/// паре «опекун и ученик». Первичный ключ нужен базе, и живёт он ровно здесь.
using RowId = core::StrongId<struct GuardianshipRowTag>;

const userver::storages::postgres::Query kFindActive{
    "SELECT granted_at FROM identity_guardianship "
    "WHERE guardian_id = $1::uuid AND student_id = $2::uuid AND revoked_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_guardianship_find_active"},
};

const userver::storages::postgres::Query kGuardiansOf{
    "SELECT guardian_id::text AS guardian_id FROM identity_guardianship "
    "WHERE student_id = $1::uuid AND revoked_at IS NULL ORDER BY granted_at",
    userver::storages::postgres::Query::Name{"identity_guardianship_guardians_of"},
};

const userver::storages::postgres::Query kUpdateActive{
    "UPDATE identity_guardianship SET granted_at = $3, revoked_at = $4 "
    "WHERE guardian_id = $1::uuid AND student_id = $2::uuid AND revoked_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_guardianship_update_active"},
};

const userver::storages::postgres::Query kInsert{
    "INSERT INTO identity_guardianship "
    "(tenant_id, id, guardian_id, student_id, granted_at, revoked_at) "
    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5, $6)",
    userver::storages::postgres::Query::Name{"identity_guardianship_insert"},
};

core::PersonId PersonOf(const userver::storages::postgres::Row& row, const char* column) {
    const auto person = core::PersonId::Parse(row[column].As<std::string>());
    if (!person.has_value()) {
        throw std::runtime_error{"identity_guardianship: строка не разбирается"};
    }
    return *person;
}

}  // namespace

PostgresGuardianships::PostgresGuardianships(infrastructure::db::ScopedTenantContext& scope,
                                             const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

std::optional<Guardianship> PostgresGuardianships::FindActive(const core::TenantId& tenant,
                                                              const core::PersonId& guardian,
                                                              const core::PersonId& student) const {
    const auto result =
        scope_.Session().Execute(kFindActive, guardian.ToString(), student.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    return Guardianship::Restore(tenant,
                                 guardian,
                                 student,
                                 AsInstant(result.Front()["granted_at"].As<Timestamptz>()),
                                 std::nullopt);
}

std::vector<core::PersonId> PostgresGuardianships::GuardiansOf(
    const core::TenantId& tenant, const core::PersonId& student) const {
    static_cast<void>(tenant);

    const auto result = scope_.Session().Execute(kGuardiansOf, student.ToString());

    std::vector<core::PersonId> found;
    found.reserve(result.Size());
    for (const auto& row : result) {
        found.push_back(PersonOf(row, "guardian_id"));
    }
    return found;
}

void PostgresGuardianships::Save(const Guardianship& guardianship) {
    std::optional<Timestamptz> revoked;
    if (guardianship.RevokedAt().has_value()) {
        revoked = AsTimestamptz(*guardianship.RevokedAt());
    }

    const auto changed = scope_.Session().Execute(kUpdateActive,
                                                  guardianship.Guardian().ToString(),
                                                  guardianship.Student().ToString(),
                                                  AsTimestamptz(guardianship.GrantedAt()),
                                                  revoked);
    if (changed.RowsAffected() > 0) {
        return;
    }

    scope_.Session().Execute(kInsert,
                             guardianship.Tenant().ToString(),
                             ids_.Next<RowId>().ToString(),
                             guardianship.Guardian().ToString(),
                             guardianship.Student().ToString(),
                             AsTimestamptz(guardianship.GrantedAt()),
                             revoked);
}

}  // namespace pdr::identity
