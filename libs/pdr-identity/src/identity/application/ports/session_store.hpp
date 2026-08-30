#pragma once

#include <optional>

#include "core/types/ids.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity::ports {

/// Хранилище сессий.
///
/// ПОРТ ПРИНИМАЕТ `SessionId` И БОЛЬШЕ НИЧЕГО. Ни запроса, ни заголовков, ни
/// cookie здесь нет и не появится: откуда приехал идентификатор — дело
/// транспорта, а транспорт живёт в infrastructure. Мобильный клиент с cookie
/// не работает вовсе и принесёт тот же идентификатор заголовком; ни одна
/// строка отсюда и из сценариев от этого не поменяется.
class SessionStore {
public:
    SessionStore(const SessionStore&) = delete;
    SessionStore& operator=(const SessionStore&) = delete;

    virtual ~SessionStore() = default;

    virtual void Save(const Session& session) = 0;

    /// Строка сессии, годная она или нет. Годность спрашивают у домена
    /// (`Session::IsUsableAt`), а не у хранилища: тогда «истекла» и «отозвана»
    /// различимы, и по ним можно ответить человеку по-разному.
    virtual std::optional<Session> Find(const SessionId& id) const = 0;

    /// Отозвать все действующие сессии человека. Нужно после смены пароля:
    /// сменивший пароль ожидает, что унесённое устройство перестало работать.
    virtual void RevokeAllFor(const core::TenantId& tenant,
                              const core::PersonId& person,
                              core::Instant at) = 0;

protected:
    SessionStore() = default;
};

}  // namespace pdr::identity::ports
