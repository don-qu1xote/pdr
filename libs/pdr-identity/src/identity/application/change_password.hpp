#pragma once

#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

struct ChangePasswordRequest final {
    SessionId current;
    std::string old_secret;
    std::string new_secret;
    Fingerprint seen;
};

/// Смена пароля.
///
/// СТАРЫЙ ИДЕНТИФИКАТОР ПЕРЕСТАЁТ РАБОТАТЬ НЕМЕДЛЕННО, и не он один: гасятся
/// ВСЕ сессии человека, включая ту, из которой пришли. Пароль меняют не от
/// хорошей жизни — чаще всего потому, что устройство унесли или его кто-то
/// увидел; смена пароля, после которой унесённое устройство продолжает
/// работать, не решает ничего.
///
/// Взамен выдаётся новая сессия — тому, кто меняет: иначе человек, сменивший
/// пароль, тут же оказывается на форме входа и вводит новый пароль вслепую.
///
/// Старый пароль спрашивается обязательно: без него открытый чужой ноутбук
/// превращается в захват учётной записи навсегда.
class ChangePassword final {
public:
    ChangePassword(const ports::AuthSettings& settings,
                   ports::CredentialStore& credentials,
                   const ports::PasswordHasher& hasher,
                   ports::SessionStore& sessions,
                   const application::ports::SecretGenerator& secrets,
                   const application::ports::Clock& clock) noexcept;

    core::Result<Session> Execute(const ChangePasswordRequest& request) const;

private:
    const ports::AuthSettings& settings_;
    ports::CredentialStore& credentials_;
    const ports::PasswordHasher& hasher_;
    ports::SessionStore& sessions_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
