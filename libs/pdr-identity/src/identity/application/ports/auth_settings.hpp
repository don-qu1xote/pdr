#pragma once

#include "core/errors.hpp"
#include "identity/core/account.hpp"
#include "identity/core/auth_lifetimes.hpp"
#include "identity/core/login_throttle.hpp"
#include "identity/core/password.hpp"

namespace pdr::identity {

namespace ports {

/// Настройки входа: стоимость счёта хеша, пороги перебора, сроки жизни.
///
/// Порт, а не константы: все три величины подбираются на живой системе, и
/// подбирать их выкаткой — значит не подбирать. За портом стоит штатный
/// dynamic_config (PDR-CFG-01).
///
/// Отказ возвращается, а не подменяется умолчанием: настройка, которую источник
/// прислал негодной, обязана выглядеть как поломка. Молча взятое умолчание
/// здесь означало бы вход по правилам, о которых никто не просил.
class AuthSettings {
public:
    AuthSettings(const AuthSettings&) = delete;
    AuthSettings& operator=(const AuthSettings&) = delete;

    virtual ~AuthSettings() = default;

    virtual core::Result<PasswordRules> Passwords() const = 0;

    virtual core::Result<ThrottleLimits> Throttle() const = 0;

    virtual core::Result<AuthLifetimes> Lifetimes() const = 0;

    /// Порог самостоятельных заведений с одного адреса.
    virtual core::Result<SignupLimits> Signups() const = 0;

protected:
    AuthSettings() = default;
};

}  // namespace ports
}  // namespace pdr::identity
