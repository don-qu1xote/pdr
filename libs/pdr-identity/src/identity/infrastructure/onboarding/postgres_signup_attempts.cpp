#include "identity/infrastructure/onboarding/postgres_signup_attempts.hpp"

#include <cstdint>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

}  // namespace

PostgresSignupAttempts::PostgresSignupAttempts(
    const infrastructure::db::UnscopedAccess& access) noexcept
    : access_{access} {}

std::optional<AttemptWindow> PostgresSignupAttempts::Window(const Digest& address) const {
    const auto result = access_.Execute(sql::kIdentitySignupAttemptWindow, address.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        result.Front().As<IdentitySignupAttemptWindowRow>(userver::storages::postgres::kRowTag);
    return AttemptWindow::Restore(AsInstant(Filled(row.window_started_at, "window_started_at")),
                                  static_cast<std::uint32_t>(Filled(row.attempts, "attempts")));
}

void PostgresSignupAttempts::Save(const Digest& address, const AttemptWindow& window) {
    access_.Execute(sql::kIdentitySignupAttemptSave,
                    address.Value(),
                    window.StartedAt(),
                    static_cast<std::int32_t>(window.Attempts()));
}

}  // namespace pdr::identity
