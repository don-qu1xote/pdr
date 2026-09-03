#include "identity/infrastructure/access/postgres_guardianships.hpp"

#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

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

core::PersonId PersonOf(const std::string& stored) {
    const auto person = core::PersonId::Parse(stored);
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
    const auto result = scope_.Session().Execute(
        sql::kIdentityGuardianshipFindActive, guardian.ToString(), student.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    return Guardianship::Restore(
        tenant,
        guardian,
        student,
        AsInstant(result.Front().As<Timestamptz>(userver::storages::postgres::kFieldTag)),
        std::nullopt);
}

std::vector<core::PersonId> PostgresGuardianships::GuardiansOf(
    const core::TenantId& tenant, const core::PersonId& student) const {
    static_cast<void>(tenant);

    const auto result =
        scope_.Session().Execute(sql::kIdentityGuardianshipGuardiansOf, student.ToString());

    std::vector<core::PersonId> found;
    found.reserve(result.Size());
    for (const auto& stored : result.AsSetOf<std::string>(userver::storages::postgres::kFieldTag)) {
        found.push_back(PersonOf(stored));
    }
    return found;
}

void PostgresGuardianships::Save(const Guardianship& guardianship) {
    std::optional<Timestamptz> revoked;
    if (guardianship.RevokedAt().has_value()) {
        revoked = AsTimestamptz(*guardianship.RevokedAt());
    }

    const auto changed = scope_.Session().Execute(sql::kIdentityGuardianshipUpdateActive,
                                                  guardianship.Guardian().ToString(),
                                                  guardianship.Student().ToString(),
                                                  AsTimestamptz(guardianship.GrantedAt()),
                                                  revoked);
    if (changed.RowsAffected() > 0) {
        return;
    }

    scope_.Session().Execute(sql::kIdentityGuardianshipInsert,
                             guardianship.Tenant().ToString(),
                             ids_.Next<RowId>().ToString(),
                             guardianship.Guardian().ToString(),
                             guardianship.Student().ToString(),
                             AsTimestamptz(guardianship.GrantedAt()),
                             revoked);
}

}  // namespace pdr::identity
