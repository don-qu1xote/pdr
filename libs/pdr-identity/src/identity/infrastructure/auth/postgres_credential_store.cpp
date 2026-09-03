#include "identity/infrastructure/auth/postgres_credential_store.hpp"

#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::Filled;

PasswordHash Restored(const std::string& stored) {
    auto hash = PasswordHash::Parse(stored);
    if (!hash) {
        throw std::runtime_error{"identity_credential.password_hash не Argon2id: " +
                                 hash.Failure().Detail()};
    }
    return hash.Value();
}

}  // namespace

PostgresCredentialStore::PostgresCredentialStore(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<ports::Credential> PostgresCredentialStore::FindByEmail(const core::TenantId&,
                                                                      const Email& mail) const {
    const auto result = scope_.Session().Execute(sql::kIdentityCredentialByEmail, mail.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        result.Front().As<IdentityCredentialByEmailRow>(userver::storages::postgres::kRowTag);
    const auto person = core::PersonId::Parse(Filled(row.person_id, "person_id"));
    if (!person.has_value()) {
        throw std::runtime_error{"identity_credential.person_id не идентификатор"};
    }

    return ports::Credential{*person, Restored(Filled(row.password_hash, "password_hash"))};
}

std::optional<PasswordHash> PostgresCredentialStore::FindFor(const core::TenantId&,
                                                             const core::PersonId& person) const {
    const auto result = scope_.Session().Execute(sql::kIdentityCredentialByPerson, person);
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    return Restored(result.Front().As<std::string>(userver::storages::postgres::kFieldTag));
}

void PostgresCredentialStore::Save(const core::TenantId& tenant,
                                   const core::PersonId& person,
                                   const PasswordHash& hash) {
    scope_.Session().Execute(sql::kIdentityCredentialSave, tenant, person, hash.Value());
}

}  // namespace pdr::identity
