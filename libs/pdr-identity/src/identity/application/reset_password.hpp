#pragma once

#include <optional>
#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/one_time_tokens.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/email.hpp"
#include "identity/core/one_time_token.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Выданная ссылка сброса. Секрет отдаётся один раз — письму, и больше нигде
/// не появляется.
struct IssuedReset final {
    OneTimeToken token;
    TokenSecret secret;
};

/// «Я забыл пароль»: выдать одноразовую ссылку.
///
/// ОТВЕТ ОДИНАКОВ ДЛЯ СУЩЕСТВУЮЩЕЙ И НЕСУЩЕСТВУЮЩЕЙ ПОЧТЫ. Успех возвращается
/// всегда, а ссылка — только когда есть кому её послать: иначе форма «забыли
/// пароль» становится способом бесплатно узнать, кто у нас учится. Поэтому
/// внутри `optional`, а не отказ: звонящему нечего показать по-разному.
class RequestPasswordReset final {
public:
    RequestPasswordReset(const ports::AuthSettings& settings,
                         const ports::CredentialStore& credentials,
                         const ports::Digests& digests,
                         ports::OneTimeTokens& tokens,
                         const application::ports::IdGenerator& ids,
                         const application::ports::SecretGenerator& secrets,
                         const application::ports::Clock& clock) noexcept;

    core::Result<std::optional<IssuedReset>> Execute(const core::TenantId& tenant,
                                                     const Email& mail) const;

private:
    const ports::AuthSettings& settings_;
    const ports::CredentialStore& credentials_;
    const ports::Digests& digests_;
    ports::OneTimeTokens& tokens_;
    const application::ports::IdGenerator& ids_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

struct ResetPasswordRequest final {
    core::TenantId tenant;
    TokenSecret secret;
    std::string password;
    Fingerprint seen;
};

/// Задать новый пароль по ссылке.
///
/// Тот же механизм одноразовых токенов, что и у приглашения, и те же
/// последствия, что и у смены пароля: гасятся ВСЕ сессии человека. Сброс
/// пароля чаще всего означает, что доступ к записи потеряли, — оставить
/// работающими старые сессии значит не сбросить ничего.
class ResetPassword final {
public:
    ResetPassword(const ports::AuthSettings& settings,
                  const ports::Digests& digests,
                  const ports::PasswordHasher& hasher,
                  ports::OneTimeTokens& tokens,
                  ports::CredentialStore& credentials,
                  ports::SessionStore& sessions,
                  const application::ports::SecretGenerator& secrets,
                  const application::ports::Clock& clock) noexcept;

    core::Result<Session> Execute(const ResetPasswordRequest& request) const;

private:
    const ports::AuthSettings& settings_;
    const ports::Digests& digests_;
    const ports::PasswordHasher& hasher_;
    ports::OneTimeTokens& tokens_;
    ports::CredentialStore& credentials_;
    ports::SessionStore& sessions_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
