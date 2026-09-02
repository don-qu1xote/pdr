#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "identity/application/ports/auth_settings.hpp"

namespace pdr::identity {

/// Настройки входа из динамического конфига.
///
/// Величины здесь ровно те, которые подбирают на живой системе: стоимость счёта
/// хеша зависит от железа, пороги перебора — от того, кто как ошибается, сроки
/// ссылок — от того, как быстро люди читают почту. Подбирать их выкаткой —
/// значит не подбирать.
///
/// СЫРЫХ СТРУКТУР ЗДЕСЬ БОЛЬШЕ НЕТ: все четыре порождены из реестра вместе с
/// ключами, умолчаниями и пределами (`dynamic_config/variables/PDR_*.hpp`).
/// Разделение «разбор отвечает что прислали, домен — годится ли это» осталось:
/// разбор теперь порождённый, а `Compose` по-прежнему доменный.
class DynamicConfigAuthSettings final : public ports::AuthSettings {
public:
    explicit DynamicConfigAuthSettings(userver::dynamic_config::Source source);

    ~DynamicConfigAuthSettings() override;

    core::Result<PasswordRules> Passwords() const override;

    core::Result<ThrottleLimits> Throttle() const override;

    core::Result<AuthLifetimes> Lifetimes() const override;

    core::Result<SignupLimits> Signups() const override;

private:
    /// Журнал «было → стало». Значения самих паролей в нём нет и не будет —
    /// только стоимость счёта: журнал, в который попадает секрет, хуже
    /// отсутствующего.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::identity
