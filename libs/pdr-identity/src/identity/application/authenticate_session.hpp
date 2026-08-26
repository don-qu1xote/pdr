#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Проверка сессии: кто пришёл.
///
/// ЭТОТ СЦЕНАРИЙ НЕ ЗНАЕТ О ТРАНСПОРТЕ НИЧЕГО. Ни cookie, ни заголовка, ни
/// запроса — на входе `SessionId`, и всё. Мобильный клиент, который с cookie
/// не работает вовсе, принесёт тот же идентификатор заголовком, и здесь не
/// поменяется ни строки; достаёт идентификатор из запроса
/// `infrastructure/http/session_transport.hpp`, и только он.
///
/// «Истекла» и «отозвана» различаются кодами намеренно: первое человек чинит
/// повторным входом, второе значит, что доступ забрали, — и это разные ответы.
class AuthenticateSession final {
public:
    AuthenticateSession(const ports::SessionStore& sessions,
                        const application::ports::Clock& clock) noexcept;

    core::Result<Session> Execute(const SessionId& id) const;

private:
    const ports::SessionStore& sessions_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
