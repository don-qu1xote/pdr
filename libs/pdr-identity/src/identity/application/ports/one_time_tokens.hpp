#pragma once

#include <optional>

#include "core/types/ids.hpp"
#include "identity/core/digest.hpp"
#include "identity/core/one_time_token.hpp"

namespace pdr::identity::ports {

/// Хранилище одноразовых токенов: приглашений и сбросов пароля.
///
/// Поиск идёт по ОТПЕЧАТКУ, а не по секрету: самого секрета здесь нет ни в
/// одном методе, и положить его в базу нечем.
class OneTimeTokens {
public:
    OneTimeTokens(const OneTimeTokens&) = delete;
    OneTimeTokens& operator=(const OneTimeTokens&) = delete;

    virtual ~OneTimeTokens() = default;

    virtual void Issue(const OneTimeToken& token) = 0;

    virtual std::optional<OneTimeToken> Find(const core::TenantId& tenant,
                                             const Digest& secret) const = 0;

    /// Отметить сработавшим. Отдельная операция, а не `Issue` поверх: строка
    /// одноразового токена не переписывается целиком — переписанную можно
    /// «разиспользовать» опечаткой.
    virtual void MarkUsed(const OneTimeToken& token) = 0;

protected:
    OneTimeTokens() = default;
};

}  // namespace pdr::identity::ports
