#include "identity/infrastructure/onboarding/postgres_signup_attempts.hpp"

#include <cstdint>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

const userver::storages::postgres::Query kWindow{
    "SELECT window_started_at, attempts FROM identity_signup_attempt WHERE address_hash = $1",
    userver::storages::postgres::Query::Name{"identity_signup_attempt_window"},
};

const userver::storages::postgres::Query kSave{
    "INSERT INTO identity_signup_attempt (address_hash, window_started_at, attempts) "
    "VALUES ($1, $2, $3) ON CONFLICT (address_hash) DO UPDATE "
    "SET window_started_at = excluded.window_started_at, attempts = excluded.attempts",
    userver::storages::postgres::Query::Name{"identity_signup_attempt_save"},
};

}  // namespace

PostgresSignupAttempts::PostgresSignupAttempts(
    const infrastructure::db::UnscopedAccess& access) noexcept
    : access_{access} {}

std::optional<AttemptWindow> PostgresSignupAttempts::Window(const Digest& address) const {
    const auto result = access_.Execute(kWindow, address.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    return AttemptWindow::Restore(AsInstant(row["window_started_at"].As<Timestamptz>()),
                                  static_cast<std::uint32_t>(row["attempts"].As<std::int32_t>()));
}

void PostgresSignupAttempts::Save(const Digest& address, const AttemptWindow& window) {
    access_.Execute(kSave,
                    address.Value(),
                    AsTimestamptz(window.StartedAt()),
                    static_cast<std::int32_t>(window.Attempts()));
}

}  // namespace pdr::identity
