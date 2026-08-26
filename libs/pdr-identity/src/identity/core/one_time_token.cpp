#include "identity/core/one_time_token.hpp"

#include <algorithm>
#include <string>

namespace pdr::identity {
namespace {

bool IsBase64Url(char symbol) noexcept {
    return (symbol >= '0' && symbol <= '9') || (symbol >= 'a' && symbol <= 'z') ||
           (symbol >= 'A' && symbol <= 'Z') || symbol == '-' || symbol == '_';
}

core::Error LifetimeNotPositive() {
    return core::Error{core::ErrorKind::kValidation,
                       "token_lifetime_not_positive",
                       "ссылка, просроченная в момент выдачи, — это не ссылка"};
}

}  // namespace

std::string_view Name(TokenPurpose purpose) noexcept {
    switch (purpose) {
        case TokenPurpose::kInvitation:
            return "invitation";
        case TokenPurpose::kPasswordReset:
            return "password_reset";
    }
    return "invitation";
}

std::optional<TokenPurpose> ParseTokenPurpose(std::string_view text) {
    if (text == "invitation") {
        return TokenPurpose::kInvitation;
    }
    if (text == "password_reset") {
        return TokenPurpose::kPasswordReset;
    }
    return std::nullopt;
}

core::Result<TokenSecret> TokenSecret::Parse(std::string_view text) {
    if (text.size() < kLeastLength) {
        return core::Error{core::ErrorKind::kValidation,
                           "token_secret_too_short",
                           "в ссылке меньше " + std::to_string(kLeastLength) +
                               " знаков: такую подбирают перебором"};
    }
    if (!std::all_of(text.begin(), text.end(), IsBase64Url)) {
        return core::Error{core::ErrorKind::kValidation,
                           "token_secret_malformed",
                           "секрет ссылки записывается base64url без набивки"};
    }

    return TokenSecret{std::string{text}};
}

core::Result<OneTimeToken> OneTimeToken::Invitation(TokenId id,
                                                    core::TenantId tenant,
                                                    Digest secret,
                                                    Role role,
                                                    core::Instant at,
                                                    core::Instant::Duration lifetime) {
    if (lifetime <= core::Instant::Duration::zero()) {
        return LifetimeNotPositive();
    }

    return OneTimeToken{std::move(id),
                        std::move(tenant),
                        std::move(secret),
                        TokenPurpose::kInvitation,
                        role,
                        std::nullopt,
                        at,
                        at + lifetime,
                        std::nullopt};
}

core::Result<OneTimeToken> OneTimeToken::PasswordReset(TokenId id,
                                                       core::TenantId tenant,
                                                       Digest secret,
                                                       core::PersonId person,
                                                       core::Instant at,
                                                       core::Instant::Duration lifetime) {
    if (lifetime <= core::Instant::Duration::zero()) {
        return LifetimeNotPositive();
    }

    return OneTimeToken{std::move(id),
                        std::move(tenant),
                        std::move(secret),
                        TokenPurpose::kPasswordReset,
                        std::nullopt,
                        std::move(person),
                        at,
                        at + lifetime,
                        std::nullopt};
}

OneTimeToken OneTimeToken::Restore(TokenId id,
                                   core::TenantId tenant,
                                   Digest secret,
                                   TokenPurpose purpose,
                                   std::optional<Role> role,
                                   std::optional<core::PersonId> person,
                                   core::Instant created_at,
                                   core::Instant expires_at,
                                   std::optional<core::Instant> used_at) {
    return OneTimeToken{std::move(id),
                        std::move(tenant),
                        std::move(secret),
                        purpose,
                        role,
                        std::move(person),
                        created_at,
                        expires_at,
                        used_at};
}

bool OneTimeToken::IsUsableAt(core::Instant moment) const noexcept {
    if (used_at_.has_value()) {
        return false;
    }
    return moment < expires_at_;
}

core::Result<OneTimeToken> OneTimeToken::Used(core::Instant at) const {
    if (used_at_.has_value()) {
        return core::Error{
            core::ErrorKind::kConflict, "token_already_used", "по этой ссылке уже заходили"};
    }
    if (at >= expires_at_) {
        return core::Error{core::ErrorKind::kConflict, "token_expired", "срок ссылки вышел"};
    }

    return OneTimeToken{
        id_, tenant_, secret_, purpose_, role_, person_, created_at_, expires_at_, at};
}

}  // namespace pdr::identity
