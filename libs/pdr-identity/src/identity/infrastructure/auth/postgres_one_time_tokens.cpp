#include "identity/infrastructure/auth/postgres_one_time_tokens.hpp"

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

    scope_.Session().Execute(sql::kIdentityOneTimeTokenIssue,
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
    const auto result = scope_.Session().Execute(sql::kIdentityOneTimeTokenFind, secret.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        result.Front().As<IdentityOneTimeTokenFindRow>(userver::storages::postgres::kRowTag);
    const auto id = TokenId::Parse(Filled(row.id, "id"));
    const auto purpose = ParseTokenPurpose(Filled(row.purpose, "purpose"));
    if (!id.has_value() || !purpose.has_value()) {
        throw std::runtime_error{"identity_one_time_token: строка не разбирается"};
    }

    std::optional<Role> role;
    if (row.role.has_value()) {
        role = ParseRole(*row.role);
        if (!role.has_value()) {
            throw std::runtime_error{"identity_one_time_token.role не роль: " + *row.role};
        }
    }

    std::optional<core::PersonId> person;
    if (row.person_id.has_value()) {
        person = core::PersonId::Parse(*row.person_id);
        if (!person.has_value()) {
            throw std::runtime_error{"identity_one_time_token.person_id не идентификатор"};
        }
    }

    std::optional<core::Instant> used;
    if (row.used_at.has_value()) {
        used = AsInstant(*row.used_at);
    }

    std::optional<Digest> invited;
    if (row.invited_digest.has_value()) {
        const auto parsed = Digest::Parse(*row.invited_digest);
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
                                 AsInstant(Filled(row.created_at, "created_at")),
                                 AsInstant(Filled(row.expires_at, "expires_at")),
                                 used);
}

std::optional<OneTimeToken> PostgresOneTimeTokens::LiveInvitationTo(const core::TenantId& tenant,
                                                                    const Digest& invited,
                                                                    core::Instant now) const {
    const auto result = scope_.Session().Execute(
        sql::kIdentityOneTimeTokenLiveInvitation, invited.Value(), AsTimestamptz(now));
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front().As<IdentityOneTimeTokenLiveInvitationRow>(
        userver::storages::postgres::kRowTag);
    const auto id = TokenId::Parse(Filled(row.id, "id"));
    const auto secret = Digest::Parse(Filled(row.token_hash, "token_hash"));
    const auto role = ParseRole(Filled(row.role, "role"));
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
                                 AsInstant(Filled(row.created_at, "created_at")),
                                 AsInstant(Filled(row.expires_at, "expires_at")),
                                 std::nullopt);
}

void PostgresOneTimeTokens::MarkUsed(const OneTimeToken& token) {
    scope_.Session().Execute(
        sql::kIdentityOneTimeTokenMarkUsed, token.Id().ToString(), AsTimestamptz(*token.UsedAt()));
}

}  // namespace pdr::identity
