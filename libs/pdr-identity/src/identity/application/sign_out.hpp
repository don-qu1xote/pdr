#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Выход.
///
/// Выход из сессии, которой нет или которая уже погашена, — успех, а не отказ:
/// «выйти» человек нажимает дважды подряд чаще, чем кажется, и второй раз
/// обязан выглядеть так же, как первый. Состояние после вызова одно и то же,
/// а это и есть то, о чём просили.
class SignOut final {
public:
    SignOut(ports::SessionStore& sessions, const application::ports::Clock& clock) noexcept;

    core::Result<void> Execute(const SessionId& id) const;

private:
    ports::SessionStore& sessions_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
