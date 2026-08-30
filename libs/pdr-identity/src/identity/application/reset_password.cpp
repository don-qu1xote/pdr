#include "identity/application/reset_password.hpp"

#include "identity/application/invite_participant.hpp"

namespace pdr::identity {

RequestPasswordReset::RequestPasswordReset(const ports::AuthSettings& settings,
                                           const ports::CredentialStore& credentials,
                                           const ports::Digests& digests,
                                           ports::OneTimeTokens& tokens,
                                           const application::ports::IdGenerator& ids,
                                           const application::ports::SecretGenerator& secrets,
                                           const application::ports::Clock& clock) noexcept
    : settings_{settings},
      credentials_{credentials},
      digests_{digests},
      tokens_{tokens},
      ids_{ids},
      secrets_{secrets},
      clock_{clock} {}

core::Result<std::optional<IssuedReset>> RequestPasswordReset::Execute(const core::TenantId& tenant,
                                                                       const Email& mail) const {
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto found = credentials_.FindByEmail(tenant, mail);
    if (!found.has_value()) {
        return std::optional<IssuedReset>{};
    }

    const auto secret = TokenSecret::Parse(secrets_.NextText(kTokenBytes));
    if (!secret) {
        return secret.Failure();
    }

    auto token = OneTimeToken::PasswordReset(ids_.Next<TokenId>(),
                                             tenant,
                                             digests_.Of(secret.Value().Value()),
                                             found->person,
                                             clock_.Now(),
                                             lifetimes.Value().PasswordReset());
    if (!token) {
        return token.Failure();
    }

    tokens_.Issue(token.Value());
    return std::optional<IssuedReset>{IssuedReset{token.Value(), secret.Value()}};
}

ResetPassword::ResetPassword(const ports::AuthSettings& settings,
                             const ports::Digests& digests,
                             const ports::PasswordHasher& hasher,
                             ports::OneTimeTokens& tokens,
                             ports::CredentialStore& credentials,
                             ports::SessionStore& sessions,
                             const application::ports::SecretGenerator& secrets,
                             const application::ports::Clock& clock) noexcept
    : settings_{settings},
      digests_{digests},
      hasher_{hasher},
      tokens_{tokens},
      credentials_{credentials},
      sessions_{sessions},
      secrets_{secrets},
      clock_{clock} {}

core::Result<Session> ResetPassword::Execute(const ResetPasswordRequest& request) const {
    const auto rules = settings_.Passwords();
    if (!rules) {
        return rules.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto now = clock_.Now();
    const auto found = tokens_.Find(request.tenant, digests_.Of(request.secret.Value()));
    if (!found.has_value() || found->Purpose() != TokenPurpose::kPasswordReset) {
        return core::Error{
            core::ErrorKind::kNotFound, "password_reset_unknown", "такой ссылки нет"};
    }

    const auto used = found->Used(now);
    if (!used) {
        return used.Failure();
    }

    const auto chosen = Password::Chosen(request.password, rules.Value());
    if (!chosen) {
        return chosen.Failure();
    }
    const auto hash = hasher_.Hash(chosen.Value(), rules.Value());
    if (!hash) {
        return hash.Failure();
    }

    const auto& person = *found->Person();

    tokens_.MarkUsed(used.Value());
    credentials_.Save(request.tenant, person, hash.Value());
    sessions_.RevokeAllFor(request.tenant, person, now);

    auto opened = Session::Open(SessionId{request.tenant, secrets_.Next<SessionSecret>()},
                                person,
                                now,
                                lifetimes.Value().Session(),
                                request.seen);
    if (!opened) {
        return opened.Failure();
    }

    sessions_.Save(opened.Value());
    return opened.Value();
}

}  // namespace pdr::identity
