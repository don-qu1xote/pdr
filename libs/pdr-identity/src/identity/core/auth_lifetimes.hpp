#pragma once

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

/// Сроки жизни того, что выдаётся человеку.
///
/// Три разных срока, потому что риск у них разный: сессия живёт долго и её
/// можно отозвать, приглашение живёт днями и срабатывает один раз, ссылка
/// сброса живёт минутами — её присылают в ответ на «я забыл», и через час она
/// не нужна никому, кроме того, кто прочитал чужую почту.
///
/// Значения приходят из динамического конфига (`PDR_AUTH_LIFETIMES`). Связь
/// между ними схема реестра не выражает, и её проверяет этот тип.
class AuthLifetimes final {
public:
    static core::Result<AuthLifetimes> Compose(core::Instant::Duration session,
                                               core::Instant::Duration invitation,
                                               core::Instant::Duration password_reset);

    core::Instant::Duration Session() const noexcept {
        return session_;
    }
    core::Instant::Duration Invitation() const noexcept {
        return invitation_;
    }
    core::Instant::Duration PasswordReset() const noexcept {
        return password_reset_;
    }

    friend bool operator==(const AuthLifetimes&, const AuthLifetimes&) = default;

private:
    AuthLifetimes(core::Instant::Duration session,
                  core::Instant::Duration invitation,
                  core::Instant::Duration password_reset) noexcept
        : session_{session}, invitation_{invitation}, password_reset_{password_reset} {}

    core::Instant::Duration session_;
    core::Instant::Duration invitation_;
    core::Instant::Duration password_reset_;
};

}  // namespace pdr::identity
