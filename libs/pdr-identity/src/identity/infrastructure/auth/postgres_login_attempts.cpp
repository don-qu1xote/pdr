#include "identity/infrastructure/auth/postgres_login_attempts.hpp"

#include <cstdint>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

/// $4 — «сейчас», $5 — «начало окна, которое уже истекло». Сравнение с $5 и
/// есть то самое правило домена, только записанное там, где оно неделимо.
const userver::storages::postgres::Query kRegister{
    "INSERT INTO identity_login_attempt "
    "(tenant_id, subject_kind, subject_hash, window_started_at, attempts) "
    "VALUES ($1::uuid, $2, $3, $4, 1) "
    "ON CONFLICT (tenant_id, subject_kind, subject_hash) DO UPDATE SET "
    "window_started_at = CASE WHEN identity_login_attempt.window_started_at <= $5 "
    "THEN excluded.window_started_at ELSE identity_login_attempt.window_started_at END, "
    "attempts = CASE WHEN identity_login_attempt.window_started_at <= $5 "
    "THEN 1 ELSE identity_login_attempt.attempts + 1 END "
    "RETURNING window_started_at, attempts",
    userver::storages::postgres::Query::Name{"identity_login_attempt_register"},
};

const userver::storages::postgres::Query kSeen{
    "SELECT window_started_at, attempts FROM identity_login_attempt "
    "WHERE subject_kind = $1 AND subject_hash = $2",
    userver::storages::postgres::Query::Name{"identity_login_attempt_seen"},
};

const userver::storages::postgres::Query kForget{
    "DELETE FROM identity_login_attempt WHERE subject_kind = $1 AND subject_hash = $2",
    userver::storages::postgres::Query::Name{"identity_login_attempt_forget"},
};

AttemptWindow From(const userver::storages::postgres::Row& row) {
    return AttemptWindow::Restore(AsInstant(row["window_started_at"].As<Timestamptz>()),
                                  row["attempts"].As<std::int32_t>());
}

}  // namespace

PostgresLoginAttempts::PostgresLoginAttempts(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

AttemptWindow PostgresLoginAttempts::Register(const core::TenantId& tenant,
                                              AttemptSubject subject,
                                              const Digest& of,
                                              core::Instant at,
                                              core::Instant::Duration window) {
    const auto result = scope_.Session().Execute(kRegister,
                                                 tenant.ToString(),
                                                 std::string{Name(subject)},
                                                 of.Value(),
                                                 AsTimestamptz(at),
                                                 AsTimestamptz(at - window));
    return From(result.Front());
}

AttemptWindow PostgresLoginAttempts::Seen(const core::TenantId&,
                                          AttemptSubject subject,
                                          const Digest& of) const {
    const auto result = scope_.Session().Execute(kSeen, std::string{Name(subject)}, of.Value());
    if (result.IsEmpty()) {
        return AttemptWindow::Restore(core::Instant::FromUnixMicros(0), 0);
    }

    return From(result.Front());
}

void PostgresLoginAttempts::Forget(const core::TenantId&,
                                   AttemptSubject subject,
                                   const Digest& of) {
    scope_.Session().Execute(kForget, std::string{Name(subject)}, of.Value());
}

}  // namespace pdr::identity
