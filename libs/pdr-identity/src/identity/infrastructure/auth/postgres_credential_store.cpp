#include "identity/infrastructure/auth/postgres_credential_store.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

namespace pdr::identity {
namespace {

const userver::storages::postgres::Query kFindByEmail{
    "SELECT c.person_id, c.password_hash "
    "FROM identity_credential c "
    "JOIN identity_person p ON p.tenant_id = c.tenant_id AND p.id = c.person_id "
    "WHERE p.email = $1",
    userver::storages::postgres::Query::Name{"identity_credential_by_email"},
};

const userver::storages::postgres::Query kFindForPerson{
    "SELECT password_hash FROM identity_credential WHERE person_id = $1::uuid",
    userver::storages::postgres::Query::Name{"identity_credential_by_person"},
};

/// Смена пароля — это правка той же строки, а не вторая строка рядом: две
/// действующие пары «человек и пароль» не выражаются в схеме, и правильно.
const userver::storages::postgres::Query kSave{
    "INSERT INTO identity_credential (tenant_id, person_id, password_hash, updated_at) "
    "VALUES ($1::uuid, $2::uuid, $3, now()) "
    "ON CONFLICT (tenant_id, person_id) DO UPDATE "
    "SET password_hash = excluded.password_hash, updated_at = excluded.updated_at",
    userver::storages::postgres::Query::Name{"identity_credential_save"},
};

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
    const auto result = scope_.Session().Execute(kFindByEmail, mail.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto person = core::PersonId::Parse(row["person_id"].As<std::string>());
    if (!person.has_value()) {
        throw std::runtime_error{"identity_credential.person_id не идентификатор"};
    }

    return ports::Credential{*person, Restored(row["password_hash"].As<std::string>())};
}

std::optional<PasswordHash> PostgresCredentialStore::FindFor(const core::TenantId&,
                                                             const core::PersonId& person) const {
    const auto result = scope_.Session().Execute(kFindForPerson, person.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    return Restored(result.Front()["password_hash"].As<std::string>());
}

void PostgresCredentialStore::Save(const core::TenantId& tenant,
                                   const core::PersonId& person,
                                   const PasswordHash& hash) {
    scope_.Session().Execute(kSave, tenant.ToString(), person.ToString(), hash.Value());
}

}  // namespace pdr::identity
