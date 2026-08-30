#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/digest.hpp"

namespace pdr::identity {

/// Секретная часть идентификатора сессии — то, что лежит в `identity_session.id`.
/// Метка своя, потому что подставить сюда идентификатор человека не должно
/// получаться даже случайно.
///
/// Значение непредсказуемое: его выдаёт `ports::SecretGenerator`, а не обычный
/// генератор идентификаторов. Разница не косметическая — обычный генератор
/// стоит на `std::mt19937`, и следующее его значение вычисляется по предыдущим.
using SessionSecret = core::StrongId<struct SessionTag>;

/// Идентификатор сессии целиком: арендатор и секрет.
///
/// АРЕНДАТОР ЗДЕСЬ НЕ ДЛЯ КРАСОТЫ. Строку сессии закрывает та же построчная
/// защита, что и всё остальное, а она отвечает только после объявления
/// арендатора (docs/architecture/tenancy.md). Значит, к моменту запроса
/// арендатор обязан быть известен — а известен он только из того, что принёс
/// сам клиент. Поэтому арендатор едет вместе с секретом, и проверка сессии
/// начинается с объявления арендатора, а не заканчивается им.
///
/// Секретности арендатор не убавляет: случайный идентификатор арендатора сам по
/// себе не даёт ничего, войти по нему нельзя, а вычислить по нему секрет —
/// тем более.
///
/// Текстовая запись — `<арендатор>.<секрет>`, и это единственное, что видит
/// транспорт. Ни cookie, ни заголовок про устройство этой строки не знают.
class SessionId final {
public:
    SessionId(core::TenantId tenant, SessionSecret secret) noexcept
        : tenant_{std::move(tenant)}, secret_{std::move(secret)} {}

    static core::Result<SessionId> Parse(std::string_view text);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const SessionSecret& Secret() const noexcept {
        return secret_;
    }

    std::string ToString() const;

    friend bool operator==(const SessionId&, const SessionId&) = default;

private:
    core::TenantId tenant_;
    SessionSecret secret_;
};

/// Отпечаток обращения: хеш строки клиента и хеш адреса.
///
/// НИ ТОГО НИ ДРУГОГО В ОТКРЫТОМ ВИДЕ МЫ НЕ ХРАНИМ. Адрес — персональные
/// данные, строка клиента вместе с адресом опознаёт человека не хуже имени; а
/// ответить на вопрос «это то же устройство, что и вчера» хеша достаточно.
/// «Сведений нет» отдельным состоянием не бывает: отпечаток пустой строки —
/// такой же отпечаток, и краевого случая с ним не возникает.
class Fingerprint final {
public:
    Fingerprint(Digest agent, Digest address) noexcept
        : agent_{std::move(agent)}, address_{std::move(address)} {}

    const Digest& Agent() const noexcept {
        return agent_;
    }
    const Digest& Address() const noexcept {
        return address_;
    }

    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;

private:
    Digest agent_;
    Digest address_;
};

/// Серверная сессия: строка в базе, а не подписанный токен у клиента.
///
/// ОТЗЫВ ОБЯЗАН РАБОТАТЬ МГНОВЕННО, и это решает всё остальное. Подписанный
/// токен без состояния действует до собственного истечения, и «выйти со всех
/// устройств», «репетитор отозвал доступ», «опекун отозвал согласие» после
/// него означают «через пятнадцать минут». У нас несовершеннолетние
/// пользователи, и пятнадцать минут — не мелочь.
///
/// Времени жизни в самой сессии нет полем: есть момент окончания. Срок
/// приходит из динамического конфига, а строка обязана пережить его правку —
/// уже выданная сессия не удлиняется и не укорачивается задним числом.
class Session final {
public:
    /// Открыть новую. Отказ ровно один: срок жизни не положительный, то есть
    /// сессия, негодная в тот же момент, когда её выдали.
    static core::Result<Session> Open(SessionId id,
                                      core::PersonId person,
                                      core::Instant at,
                                      core::Instant::Duration lifetime,
                                      Fingerprint seen);

    /// Собрать из хранилища: там строка уже прошла ограничения схемы.
    static Session Restore(SessionId id,
                           core::PersonId person,
                           core::Instant created_at,
                           core::Instant expires_at,
                           std::optional<core::Instant> revoked_at,
                           Fingerprint seen);

    const SessionId& Id() const noexcept {
        return id_;
    }
    const core::TenantId& Tenant() const noexcept {
        return id_.Tenant();
    }
    const core::PersonId& Person() const noexcept {
        return person_;
    }
    core::Instant CreatedAt() const noexcept {
        return created_at_;
    }
    core::Instant ExpiresAt() const noexcept {
        return expires_at_;
    }
    const std::optional<core::Instant>& RevokedAt() const noexcept {
        return revoked_at_;
    }
    const Fingerprint& Seen() const noexcept {
        return seen_;
    }

    /// Годна ли сессия в этот момент. Отозванная не годна никогда, истёкшая —
    /// с момента истечения. Ровно этот вопрос задаёт проверка сессии, и другого
    /// у неё нет.
    bool IsUsableAt(core::Instant moment) const noexcept;

    /// Отозвать. Повторный отзыв — не ошибка и не отказ: «выйти» дважды подряд
    /// человек нажимает чаще, чем кажется, и второе нажатие обязано выглядеть
    /// так же, как первое. Момент отзыва при этом не переписывается.
    Session Revoked(core::Instant at) const;

    friend bool operator==(const Session&, const Session&) = default;

private:
    Session(SessionId id,
            core::PersonId person,
            core::Instant created_at,
            core::Instant expires_at,
            std::optional<core::Instant> revoked_at,
            Fingerprint seen) noexcept
        : id_{std::move(id)},
          person_{std::move(person)},
          created_at_{created_at},
          expires_at_{expires_at},
          revoked_at_{revoked_at},
          seen_{std::move(seen)} {}

    SessionId id_;
    core::PersonId person_;
    core::Instant created_at_;
    core::Instant expires_at_;
    std::optional<core::Instant> revoked_at_;
    Fingerprint seen_;
};

}  // namespace pdr::identity
