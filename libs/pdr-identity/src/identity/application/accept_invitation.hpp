#pragma once

#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/one_time_tokens.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/email.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Что человек заполняет, придя по ссылке. Арендатор приходит из самой ссылки,
/// а не выбирается человеком.
struct AcceptInvitationRequest final {
    core::TenantId tenant;
    TokenSecret secret;
    std::string display_name;
    Email mail;
    BirthDate born_on;
    core::TimeZone zone;
    std::string password;
    Fingerprint seen;
};

/// Прийти по приглашению: человек появляется и сразу входит.
///
/// Сразу — потому что заставлять человека, только что задавшего себе пароль,
/// тут же вводить его на форме входа незачем: он уже доказал, что владеет
/// ссылкой и знает свой пароль.
///
/// Ссылка гасится в ту же операцию: пересланная в общий чат ссылка не должна
/// пускать второго.
class AcceptInvitation final {
public:
    AcceptInvitation(const ports::AuthSettings& settings,
                     const ports::Digests& digests,
                     const ports::PasswordHasher& hasher,
                     ports::OneTimeTokens& tokens,
                     ports::ParticipantDirectory& directory,
                     ports::CredentialStore& credentials,
                     ports::SessionStore& sessions,
                     const application::ports::IdGenerator& ids,
                     const application::ports::SecretGenerator& secrets,
                     const application::ports::Clock& clock) noexcept;

    core::Result<Session> Execute(const AcceptInvitationRequest& request) const;

private:
    const ports::AuthSettings& settings_;
    const ports::Digests& digests_;
    const ports::PasswordHasher& hasher_;
    ports::OneTimeTokens& tokens_;
    ports::ParticipantDirectory& directory_;
    ports::CredentialStore& credentials_;
    ports::SessionStore& sessions_;
    const application::ports::IdGenerator& ids_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
