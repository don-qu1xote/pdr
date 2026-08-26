#pragma once

#include <optional>
#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/login_attempts.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/email.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Чем человек представился.
///
/// Арендатор приходит вместе с почтой, а не выясняется по ней: почта уникальна
/// ВНУТРИ арендатора (`identity_person_email_unique`), и одна и та же почта у
/// двух репетиторов — это два разных человека. Откуда транспорт узнаёт
/// арендатора — его дело: адрес кабинета, поддомен, выбор из списка.
///
/// `current` — сессия, с которой пришли на форму входа. Она ОБЯЗАНА перестать
/// работать: иначе посторонний, подсунувший свой идентификатор до входа,
/// получает его же после входа — уже от чужого имени.
struct SignInRequest final {
    core::TenantId tenant;
    Email mail;
    std::string secret;
    Fingerprint seen;
    std::optional<SessionId> current;
};

/// Вход: кто это.
///
/// Правами этот сценарий не занимается вовсе — что человеку позволено, решает
/// PDR-IDENT-03. Здесь только «тот ли это, за кого себя выдаёт».
///
/// НЕУДАЧИ НЕРАЗЛИЧИМЫ. Нет такой записи, есть запись без пароля, пароль не
/// подошёл — ответ один и тот же, и стоит он одинаково по времени: иначе форма
/// входа превращается в способ узнать, кто у нас учится.
class SignIn final {
public:
    SignIn(const ports::AuthSettings& settings,
           const ports::CredentialStore& credentials,
           const ports::PasswordHasher& hasher,
           const ports::Digests& digests,
           ports::LoginAttempts& attempts,
           ports::SessionStore& sessions,
           const application::ports::SecretGenerator& secrets,
           const application::ports::Clock& clock) noexcept;

    core::Result<Session> Execute(const SignInRequest& request) const;

private:
    const ports::AuthSettings& settings_;
    const ports::CredentialStore& credentials_;
    const ports::PasswordHasher& hasher_;
    const ports::Digests& digests_;
    ports::LoginAttempts& attempts_;
    ports::SessionStore& sessions_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

/// Отпечаток учётной записи для счётчика попыток: арендатор и почта вместе.
///
/// Вместе — потому что счёт идёт по записи, а запись это пара. Отпечатком —
/// потому что счётчику незачем хранить чужую почту, чтобы прибавить единицу.
Digest AccountFingerprint(const ports::Digests& digests,
                          const core::TenantId& tenant,
                          const Email& mail);

}  // namespace pdr::identity
