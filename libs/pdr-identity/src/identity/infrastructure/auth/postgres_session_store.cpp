#include "identity/infrastructure/auth/postgres_session_store.hpp"

#include <optional>
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

Digest Restored(const std::string& stored) {
    auto digest = Digest::Parse(stored);
    if (!digest) {
        throw std::runtime_error{"identity_session: отпечаток в строке не отпечаток: " +
                                 digest.Failure().Detail()};
    }
    return digest.Value();
}

}  // namespace

PostgresSessionStore::PostgresSessionStore(infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

void PostgresSessionStore::Save(const Session& session) {
    std::optional<Timestamptz> revoked;
    if (session.RevokedAt().has_value()) {
        revoked = AsTimestamptz(*session.RevokedAt());
    }

    scope_.Session().Execute(sql::kIdentitySessionSave,
                             session.Tenant().ToString(),
                             session.Id().Secret().ToString(),
                             session.Person().ToString(),
                             AsTimestamptz(session.CreatedAt()),
                             AsTimestamptz(session.ExpiresAt()),
                             revoked,
                             session.Seen().Agent().Value(),
                             session.Seen().Address().Value());
}

std::optional<Session> PostgresSessionStore::Find(const SessionId& id) const {
    const auto result = scope_.Session().Execute(sql::kIdentitySessionFind, id.Secret().ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        result.Front().As<IdentitySessionFindRow>(userver::storages::postgres::kRowTag);
    const auto person = core::PersonId::Parse(Filled(row.person_id, "person_id"));
    if (!person.has_value()) {
        throw std::runtime_error{"identity_session.person_id не идентификатор"};
    }

    std::optional<core::Instant> revoked;
    if (row.revoked_at.has_value()) {
        revoked = AsInstant(*row.revoked_at);
    }

    return Session::Restore(id,
                            *person,
                            AsInstant(Filled(row.created_at, "created_at")),
                            AsInstant(Filled(row.expires_at, "expires_at")),
                            revoked,
                            Fingerprint{Restored(Filled(row.user_agent_hash, "user_agent_hash")),
                                        Restored(Filled(row.ip_hash, "ip_hash"))});
}

void PostgresSessionStore::RevokeAllFor(const core::TenantId&,
                                        const core::PersonId& person,
                                        core::Instant at) {
    scope_.Session().Execute(sql::kIdentitySessionRevokeAll, person.ToString(), AsTimestamptz(at));
}

}  // namespace pdr::identity
