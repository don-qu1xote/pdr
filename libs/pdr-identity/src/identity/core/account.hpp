#pragma once

#include <cstdint>
#include <optional>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/digest.hpp"

namespace pdr::identity {

/// Учётная запись: ОДИН ЧЕЛОВЕК НА ВСЮ ПЛОЩАДКУ, а не на одну практику.
///
/// Ради этого она и заведена. Математику ведёт один репетитор, английский —
/// другой, и это обычное дело, а не краевой случай. Если бы каждая практика
/// заводила своего человека заново, «один и тот же ученик» не выражалось бы
/// вовсе: у Маши было бы два разных идентификатора, и склеить их потом можно
/// было бы только руками и наугад.
///
/// ПЕРСОНАЛЬНЫХ ДАННЫХ ЗДЕСЬ НЕТ. Учётная запись — единственное, что пересекает
/// границу арендатора, поэтому в ней лежит отпечаток почты, а не почта: по
/// отпечатку человек находится, а из отпечатка не восстанавливается ни адрес,
/// ни имя, ни чей он ученик. Всё остальное живёт в `identity_person` — внутри
/// практики, под построчной защитой.
///
/// ЗДЕСЬ НЕТ И НЕ БУДЕТ НИЧЕГО УЧЕБНОГО. Ни прогресса, ни целей, ни числа
/// «готовности вообще»: как только такое число появилось бы, пришлось бы
/// решать, чей предмет в нём главный, а верного ответа нет. Состав колонок
/// заперт списком в `scripts/check_rls.py`.
class Account final {
public:
    /// Пришёл сам: почта ещё не подтверждена, и до подтверждения он никого не
    /// беспокоит.
    static core::Result<Account> Registered(core::PersonId id,
                                            Digest mail,
                                            Digest confirmation,
                                            core::Instant at,
                                            core::Instant::Duration lifetime);

    /// Пришёл по приглашению: подтверждать нечего. Письмо со ссылкой дошло —
    /// значит адрес рабочий, и второе письмо «подтвердите почту» человеку,
    /// который только что открыл письмо, ничего не проверяет.
    static Account Invited(core::PersonId id, Digest mail, core::Instant at) noexcept;

    static Account Restore(core::PersonId id,
                           Digest mail,
                           std::optional<core::Instant> confirmed_at,
                           std::optional<Digest> confirmation,
                           std::optional<core::Instant> confirmation_expires_at,
                           core::Instant created_at) noexcept;

    const core::PersonId& Id() const noexcept {
        return id_;
    }
    const Digest& Mail() const noexcept {
        return mail_;
    }
    const std::optional<core::Instant>& ConfirmedAt() const noexcept {
        return confirmed_at_;
    }
    const std::optional<Digest>& Confirmation() const noexcept {
        return confirmation_;
    }
    const std::optional<core::Instant>& ConfirmationExpiresAt() const noexcept {
        return confirmation_expires_at_;
    }
    core::Instant CreatedAt() const noexcept {
        return created_at_;
    }

    bool IsConfirmed() const noexcept {
        return confirmed_at_.has_value();
    }

    /// Подтвердить почту предъявленным секретом.
    ///
    /// Сверка здесь, а не в сценарии: «сравнить отпечатки и посмотреть на срок»
    /// — это правило, и написанное в сценарии оно однажды окажется написанным
    /// в двух сценариях по-разному.
    core::Result<Account> ConfirmedBy(const Digest& shown, core::Instant at) const;

    friend bool operator==(const Account&, const Account&) = default;

private:
    Account(core::PersonId id,
            Digest mail,
            std::optional<core::Instant> confirmed_at,
            std::optional<Digest> confirmation,
            std::optional<core::Instant> confirmation_expires_at,
            core::Instant created_at) noexcept
        : id_{std::move(id)},
          mail_{std::move(mail)},
          confirmed_at_{confirmed_at},
          confirmation_{std::move(confirmation)},
          confirmation_expires_at_{confirmation_expires_at},
          created_at_{created_at} {}

    core::PersonId id_;
    Digest mail_;
    std::optional<core::Instant> confirmed_at_;
    std::optional<Digest> confirmation_;
    std::optional<core::Instant> confirmation_expires_at_;
    core::Instant created_at_;
};

/// Сколько заведений подряд с одного адреса считается перебором.
///
/// Порог стоит на САМОСТОЯТЕЛЬНОЙ регистрации и только на ней: приглашение
/// выдаёт репетитор, и ограничивать его в собственной практике незачем.
///
/// Значения приходят из динамического конфига (`PDR_SIGNUP_THROTTLE`): порог,
/// который нельзя опустить во время наплыва и поднять после, — не защита, а
/// ещё одна выкатка.
class SignupLimits final {
public:
    static core::Result<SignupLimits> Compose(core::Instant::Duration window,
                                              std::uint32_t per_address);

    core::Instant::Duration Window() const noexcept {
        return window_;
    }
    std::uint32_t PerAddress() const noexcept {
        return per_address_;
    }

    friend bool operator==(const SignupLimits&, const SignupLimits&) = default;

private:
    SignupLimits(core::Instant::Duration window, std::uint32_t per_address) noexcept
        : window_{window}, per_address_{per_address} {}

    core::Instant::Duration window_;
    std::uint32_t per_address_;
};

}  // namespace pdr::identity
