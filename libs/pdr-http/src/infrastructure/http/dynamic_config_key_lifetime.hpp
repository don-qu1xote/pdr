#pragma once

#include <cstdint>
#include <string_view>

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/formats/parse/to.hpp>

#include "core/errors.hpp"
#include "core/idempotency.hpp"

namespace pdr::infrastructure::http {

struct IdempotencyConfig final {
    std::int32_t lifetime_hours{};
};

extern const userver::dynamic_config::Key<IdempotencyConfig> kIdempotency;

IdempotencyConfig Parse(const userver::formats::json::Value& value,
                        userver::formats::parse::To<IdempotencyConfig>);

/// Срок жизни ключа повтора из динамического конфига.
///
/// Значение живёт не константой: срок подбирают под то, как быстро клиенты
/// повторяют оборвавшийся запрос, и менять его передеплоем — значит не менять
/// его никогда. Пределы задаёт домен (`pdr::http::KeyLifetime::Compose`), и
/// негодное значение отвергается целиком: прежний срок продолжает действовать.
class DynamicConfigKeyLifetime final {
public:
    static constexpr std::string_view kIdempotencyVariable = "PDR_IDEMPOTENCY";

    explicit DynamicConfigKeyLifetime(userver::dynamic_config::Source source);

    ~DynamicConfigKeyLifetime();

    DynamicConfigKeyLifetime(const DynamicConfigKeyLifetime&) = delete;
    DynamicConfigKeyLifetime& operator=(const DynamicConfigKeyLifetime&) = delete;

    core::Result<pdr::http::KeyLifetime> Lifetime() const;

private:
    /// Журнал «было → стало». Срок повтора меняют редко; запись о смене —
    /// единственное, по чему потом поймут, почему повтор перестал приходить
    /// сохранённым ответом.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::infrastructure::http
