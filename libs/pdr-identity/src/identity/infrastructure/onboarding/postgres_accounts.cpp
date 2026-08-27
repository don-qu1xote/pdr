#include "identity/infrastructure/onboarding/postgres_accounts.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

const userver::storages::postgres::Query kByMail{
    "SELECT id, email_digest, confirmed_at, confirmation_digest, confirmation_expires_at, "
    "created_at FROM identity_account WHERE email_digest = $1",
    userver::storages::postgres::Query::Name{"identity_account_by_mail"},
};

const userver::storages::postgres::Query kById{
    "SELECT id, email_digest, confirmed_at, confirmation_digest, confirmation_expires_at, "
    "created_at FROM identity_account WHERE id = $1::uuid",
    userver::storages::postgres::Query::Name{"identity_account_by_id"},
};

const userver::storages::postgres::Query kSave{
    "INSERT INTO identity_account (id, email_digest, confirmed_at, confirmation_digest, "
    "confirmation_expires_at, created_at) VALUES ($1::uuid, $2, $3, $4, $5, $6) "
    "ON CONFLICT (id) DO UPDATE SET confirmed_at = excluded.confirmed_at, "
    "confirmation_digest = excluded.confirmation_digest, "
    "confirmation_expires_at = excluded.confirmation_expires_at",
    userver::storages::postgres::Query::Name{"identity_account_save"},
};

std::optional<Account> Parse(const userver::storages::postgres::ResultSet& result) {
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto id = core::PersonId::Parse(row["id"].As<std::string>());
    const auto mail = Digest::Parse(row["email_digest"].As<std::string>());
    if (!id.has_value() || !mail) {
        throw std::runtime_error{"identity_account: строка не разбирается"};
    }

    std::optional<core::Instant> confirmed;
    const auto stored_confirmed = row["confirmed_at"].As<std::optional<Timestamptz>>();
    if (stored_confirmed.has_value()) {
        confirmed = AsInstant(*stored_confirmed);
    }

    std::optional<Digest> confirmation;
    const auto stored_confirmation = row["confirmation_digest"].As<std::optional<std::string>>();
    if (stored_confirmation.has_value()) {
        const auto parsed = Digest::Parse(*stored_confirmation);
        if (!parsed) {
            throw std::runtime_error{"identity_account.confirmation_digest не отпечаток"};
        }
        confirmation = parsed.Value();
    }

    std::optional<core::Instant> expires;
    const auto stored_expires = row["confirmation_expires_at"].As<std::optional<Timestamptz>>();
    if (stored_expires.has_value()) {
        expires = AsInstant(*stored_expires);
    }

    return Account::Restore(*id,
                            mail.Value(),
                            confirmed,
                            confirmation,
                            expires,
                            AsInstant(row["created_at"].As<Timestamptz>()));
}

}  // namespace

PostgresAccounts::PostgresAccounts(const infrastructure::db::UnscopedAccess& access) noexcept
    : access_{access} {}

std::optional<Account> PostgresAccounts::FindByMail(const Digest& mail) const {
    return Parse(access_.Execute(kByMail, mail.Value()));
}

std::optional<Account> PostgresAccounts::FindById(const core::PersonId& id) const {
    return Parse(access_.Execute(kById, id.ToString()));
}

void PostgresAccounts::Save(const Account& account) {
    std::optional<Timestamptz> confirmed;
    if (account.ConfirmedAt().has_value()) {
        confirmed = AsTimestamptz(*account.ConfirmedAt());
    }

    std::optional<std::string> confirmation;
    if (account.Confirmation().has_value()) {
        confirmation = account.Confirmation()->Value();
    }

    std::optional<Timestamptz> expires;
    if (account.ConfirmationExpiresAt().has_value()) {
        expires = AsTimestamptz(*account.ConfirmationExpiresAt());
    }

    access_.Execute(kSave,
                    account.Id().ToString(),
                    account.Mail().Value(),
                    confirmed,
                    confirmation,
                    expires,
                    AsTimestamptz(account.CreatedAt()));
}

}  // namespace pdr::identity
