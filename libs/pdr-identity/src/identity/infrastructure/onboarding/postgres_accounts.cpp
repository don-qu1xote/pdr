#include "identity/infrastructure/onboarding/postgres_accounts.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

/// Два запроса — по почте и по идентификатору — отдают ОДИН состав колонок, и
/// у каждого своя порождённая структура. Разбор один на оба: разойдись их
/// состав, шаблон перестанет собираться сразу на обоих.
template<typename Row>
std::optional<Account> Parse(const userver::storages::postgres::ResultSet& result) {
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front().template As<Row>(userver::storages::postgres::kRowTag);
    const auto id = core::PersonId::Parse(Filled(row.id, "id"));
    const auto mail = Digest::Parse(Filled(row.email_digest, "email_digest"));
    if (!id.has_value() || !mail) {
        throw std::runtime_error{"identity_account: строка не разбирается"};
    }

    std::optional<core::Instant> confirmed;
    if (row.confirmed_at.has_value()) {
        confirmed = AsInstant(*row.confirmed_at);
    }

    std::optional<Digest> confirmation;
    if (row.confirmation_digest.has_value()) {
        const auto parsed = Digest::Parse(*row.confirmation_digest);
        if (!parsed) {
            throw std::runtime_error{"identity_account.confirmation_digest не отпечаток"};
        }
        confirmation = parsed.Value();
    }

    std::optional<core::Instant> expires;
    if (row.confirmation_expires_at.has_value()) {
        expires = AsInstant(*row.confirmation_expires_at);
    }

    return Account::Restore(*id,
                            mail.Value(),
                            confirmed,
                            confirmation,
                            expires,
                            AsInstant(Filled(row.created_at, "created_at")));
}

}  // namespace

PostgresAccounts::PostgresAccounts(const infrastructure::db::UnscopedAccess& access) noexcept
    : access_{access} {}

std::optional<Account> PostgresAccounts::FindByMail(const Digest& mail) const {
    return Parse<IdentityAccountByMailRow>(
        access_.Execute(sql::kIdentityAccountByMail, mail.Value()));
}

std::optional<Account> PostgresAccounts::FindById(const core::PersonId& id) const {
    return Parse<IdentityAccountByIdRow>(access_.Execute(sql::kIdentityAccountById, id));
}

void PostgresAccounts::Save(const Account& account) {
    std::optional<std::string> confirmation;
    if (account.Confirmation().has_value()) {
        confirmation = account.Confirmation()->Value();
    }

    access_.Execute(sql::kIdentityAccountSave,
                    account.Id(),
                    account.Mail().Value(),
                    account.ConfirmedAt(),
                    confirmation,
                    account.ConfirmationExpiresAt(),
                    account.CreatedAt());
}

}  // namespace pdr::identity
