#include "identity/infrastructure/auth/postgres_session_store.hpp"

#include <optional>
#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

/// Одна операция и на выдачу, и на отзыв. Меняются только `revoked_at`: всё
/// остальное у выданной сессии не двигается — ни срок, ни отпечаток. Продлить
/// сессию правкой строки нельзя, и это осознанно.
const userver::storages::postgres::Query kSave{
    "INSERT INTO identity_session (tenant_id, id, person_id, created_at, expires_at, "
    "revoked_at, user_agent_hash, ip_hash) "
    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4, $5, $6, $7, $8) "
    "ON CONFLICT (tenant_id, id) DO UPDATE SET revoked_at = excluded.revoked_at",
    userver::storages::postgres::Query::Name{"identity_session_save"},
};

const userver::storages::postgres::Query kFind{
    "SELECT person_id::text AS person_id, created_at, expires_at, revoked_at, "
    "user_agent_hash, ip_hash "
    "FROM identity_session WHERE id = $1::uuid",
    userver::storages::postgres::Query::Name{"identity_session_find"},
};

/// Гасятся только живые: у погашенной момент отзыва не переписывается, иначе
/// «когда этот доступ забрали» отвечает последняя смена пароля, а не первая.
const userver::storages::postgres::Query kRevokeAll{
    "UPDATE identity_session SET revoked_at = $2 "
    "WHERE person_id = $1::uuid AND revoked_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_session_revoke_all"},
};

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

    scope_.Session().Execute(kSave,
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
    const auto result = scope_.Session().Execute(kFind, id.Secret().ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto person = core::PersonId::Parse(row["person_id"].As<std::string>());
    if (!person.has_value()) {
        throw std::runtime_error{"identity_session.person_id не идентификатор"};
    }

    std::optional<core::Instant> revoked;
    const auto stored = row["revoked_at"].As<std::optional<Timestamptz>>();
    if (stored.has_value()) {
        revoked = AsInstant(*stored);
    }

    return Session::Restore(id,
                            *person,
                            AsInstant(row["created_at"].As<Timestamptz>()),
                            AsInstant(row["expires_at"].As<Timestamptz>()),
                            revoked,
                            Fingerprint{Restored(row["user_agent_hash"].As<std::string>()),
                                        Restored(row["ip_hash"].As<std::string>())});
}

void PostgresSessionStore::RevokeAllFor(const core::TenantId&,
                                        const core::PersonId& person,
                                        core::Instant at) {
    scope_.Session().Execute(kRevokeAll, person.ToString(), AsTimestamptz(at));
}

}  // namespace pdr::identity
