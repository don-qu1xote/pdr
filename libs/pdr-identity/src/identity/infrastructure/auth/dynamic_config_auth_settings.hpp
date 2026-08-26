#pragma once

#include <cstdint>
#include <string_view>

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/to.hpp>

#include "identity/application/ports/auth_settings.hpp"

namespace pdr::identity {

/// Сырые значения из конфига — до того, как их проверил домен.
///
/// Отдельная структура нужна затем, чтобы разбор JSON и правило предметной
/// области не смешивались: разбор отвечает «что прислали», домен — «годится ли
/// это». Тогда негодная запись отвергается целиком, а старая продолжает
/// действовать; иначе половина новых значений применилась бы, а половина нет.
struct PasswordRulesConfig final {
    std::uint32_t memory_kib{};
    std::uint32_t iterations{};
    std::uint32_t parallelism{};
    std::uint32_t min_length{};
};

struct ThrottleConfig final {
    std::uint32_t window_minutes{};
    std::uint32_t per_account{};
    std::uint32_t per_address{};
};

struct LifetimesConfig final {
    std::uint32_t session_hours{};
    std::uint32_t invitation_hours{};
    std::uint32_t password_reset_minutes{};
};

/// Ключи переменных — по одному объекту на всё дерево. Второй
/// `dynamic_config::Key` с тем же именем даёт вторую ячейку хранилища: подмена
/// значения в тесте тогда не доходит до адаптера, а выглядит это как «конфиг не
/// применился».
extern const userver::dynamic_config::Key<PasswordRulesConfig> kPasswordRules;
extern const userver::dynamic_config::Key<ThrottleConfig> kLoginThrottle;
extern const userver::dynamic_config::Key<LifetimesConfig> kAuthLifetimes;

/// Найдены по ADL из `pdr::identity`, как того требует userver.
PasswordRulesConfig Parse(const userver::formats::json::Value& value,
                          userver::formats::parse::To<PasswordRulesConfig>);
ThrottleConfig Parse(const userver::formats::json::Value& value,
                     userver::formats::parse::To<ThrottleConfig>);
LifetimesConfig Parse(const userver::formats::json::Value& value,
                      userver::formats::parse::To<LifetimesConfig>);

/// Настройки входа из динамического конфига.
///
/// Величины здесь ровно те, которые подбирают на живой системе: стоимость счёта
/// хеша зависит от железа, пороги перебора — от того, кто как ошибается, сроки
/// ссылок — от того, как быстро люди читают почту. Подбирать их выкаткой —
/// значит не подбирать.
class DynamicConfigAuthSettings final : public ports::AuthSettings {
public:
    static constexpr std::string_view kPasswordVariable = "PDR_SIGN_IN_RULES";
    static constexpr std::string_view kThrottleVariable = "PDR_LOGIN_THROTTLE";
    static constexpr std::string_view kLifetimesVariable = "PDR_AUTH_LIFETIMES";

    explicit DynamicConfigAuthSettings(userver::dynamic_config::Source source);

    ~DynamicConfigAuthSettings() override;

    core::Result<PasswordRules> Passwords() const override;

    core::Result<ThrottleLimits> Throttle() const override;

    core::Result<AuthLifetimes> Lifetimes() const override;

private:
    /// Журнал «было → стало». Значения самих паролей в нём нет и не будет —
    /// только стоимость счёта: журнал, в который попадает секрет, хуже
    /// отсутствующего.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::identity
