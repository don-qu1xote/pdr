#include "identity/infrastructure/auth/postgres_one_time_tokens.hpp"

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

const userver::storages::postgres::Query kIssue{
    "INSERT INTO identity_one_time_token (tenant_id, id, purpose, token_hash, role, person_id, "
    "invited_digest, created_at, expires_at) "
    "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6::uuid, $7, $8, $9)",
    userver::storages::postgres::Query::Name{"identity_one_time_token_issue"},
};

const userver::storages::postgres::Query kFind{
    "SELECT id::text AS id, purpose, role, person_id::text AS person_id, invited_digest, "
    "created_at, expires_at, used_at "
    "FROM identity_one_time_token WHERE token_hash = $1",
    userver::storages::postgres::Query::Name{"identity_one_time_token_find"},
};

/// Кого уже позвали. Условия по арендатору в запросе нет: его добавляет
/// построчная защита, и чужого приглашения этот вопрос не покажет.
const userver::storages::postgres::Query kLiveInvitation{
    "SELECT id::text AS id, token_hash, role, created_at, expires_at "
    "FROM identity_one_time_token "
    "WHERE purpose = 'invitation' AND invited_digest = $1 AND used_at IS NULL "
    "AND expires_at > $2 LIMIT 1",
    userver::storages::postgres::Query::Name{"identity_one_time_token_live_invitation"},
};

/// Условие `used_at IS NULL` стоит в самом запросе, а не только в домене: две
/// одновременные попытки по одной ссылке иначе прошли бы обе, и приглашение
/// сработало бы дважды. Здесь второй достаётся ноль изменённых строк.
const userver::storages::postgres::Query kMarkUsed{
    "UPDATE identity_one_time_token SET used_at = $2 "
    "WHERE id = $1::uuid AND used_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_one_time_token_mark_used"},
};

}  // namespace

PostgresOneTimeTokens::PostgresOneTimeTokens(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

void PostgresOneTimeTokens::Issue(const OneTimeToken& token) {
    std::optional<std::string> role;
    if (token.InvitedAs().has_value()) {
        role = std::string{Name(*token.InvitedAs())};
    }

    std::optional<std::string> person;
    if (token.Person().has_value()) {
        person = token.Person()->ToString();
    }

    std::optional<std::string> invited;
    if (token.Invited().has_value()) {
        invited = token.Invited()->Value();
    }

    scope_.Session().Execute(kIssue,
                             token.Tenant().ToString(),
                             token.Id().ToString(),
                             std::string{Name(token.Purpose())},
                             token.Secret().Value(),
                             role,
                             person,
                             invited,
                             AsTimestamptz(token.CreatedAt()),
                             AsTimestamptz(token.ExpiresAt()));
}

std::optional<OneTimeToken> PostgresOneTimeTokens::Find(const core::TenantId& tenant,
                                                        const Digest& secret) const {
    const auto result = scope_.Session().Execute(kFind, secret.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto id = TokenId::Parse(row["id"].As<std::string>());
    const auto purpose = ParseTokenPurpose(row["purpose"].As<std::string>());
    if (!id.has_value() || !purpose.has_value()) {
        throw std::runtime_error{"identity_one_time_token: строка не разбирается"};
    }

    std::optional<Role> role;
    const auto stored_role = row["role"].As<std::optional<std::string>>();
    if (stored_role.has_value()) {
        role = ParseRole(*stored_role);
        if (!role.has_value()) {
            throw std::runtime_error{"identity_one_time_token.role не роль: " + *stored_role};
        }
    }

    std::optional<core::PersonId> person;
    const auto stored_person = row["person_id"].As<std::optional<std::string>>();
    if (stored_person.has_value()) {
        person = core::PersonId::Parse(*stored_person);
        if (!person.has_value()) {
            throw std::runtime_error{"identity_one_time_token.person_id не идентификатор"};
        }
    }

    std::optional<core::Instant> used;
    const auto stored_used = row["used_at"].As<std::optional<Timestamptz>>();
    if (stored_used.has_value()) {
        used = AsInstant(*stored_used);
    }

    std::optional<Digest> invited;
    const auto stored_invited = row["invited_digest"].As<std::optional<std::string>>();
    if (stored_invited.has_value()) {
        const auto parsed = Digest::Parse(*stored_invited);
        if (!parsed) {
            throw std::runtime_error{"identity_one_time_token.invited_digest не отпечаток"};
        }
        invited = parsed.Value();
    }

    return OneTimeToken::Restore(*id,
                                 tenant,
                                 secret,
                                 *purpose,
                                 role,
                                 person,
                                 invited,
                                 AsInstant(row["created_at"].As<Timestamptz>()),
                                 AsInstant(row["expires_at"].As<Timestamptz>()),
                                 used);
}

std::optional<OneTimeToken> PostgresOneTimeTokens::LiveInvitationTo(const core::TenantId& tenant,
                                                                    const Digest& invited,
                                                                    core::Instant now) const {
    const auto result =
        scope_.Session().Execute(kLiveInvitation, invited.Value(), AsTimestamptz(now));
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto id = TokenId::Parse(row["id"].As<std::string>());
    const auto secret = Digest::Parse(row["token_hash"].As<std::string>());
    const auto role = ParseRole(row["role"].As<std::string>());
    if (!id.has_value() || !secret || !role.has_value()) {
        throw std::runtime_error{"identity_one_time_token: строка приглашения не разбирается"};
    }

    return OneTimeToken::Restore(*id,
                                 tenant,
                                 secret.Value(),
                                 TokenPurpose::kInvitation,
                                 role,
                                 std::nullopt,
                                 invited,
                                 AsInstant(row["created_at"].As<Timestamptz>()),
                                 AsInstant(row["expires_at"].As<Timestamptz>()),
                                 std::nullopt);
}

void PostgresOneTimeTokens::MarkUsed(const OneTimeToken& token) {
    scope_.Session().Execute(kMarkUsed, token.Id().ToString(), AsTimestamptz(*token.UsedAt()));
}

}  // namespace pdr::identity
