#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "core/types/time.hpp"
#include "notifications/application/ports/retry_policies.hpp"

namespace pdr::notifications {

/// Настройки разбора очереди из динамического конфига (`PDR_OUTBOX`).
///
/// Одна запись на все пять величин, потому что они одна настройка: предел
/// попыток без задержки между ними — не настройка, а стук в дверь, а срок
/// попытки, разошедшийся с задержкой повтора, даёт второе письмо вместо
/// повтора. Правят их разом и одним человеком.
///
/// Величины ТЕХНИЧЕСКИЕ и общие на площадку: сколько раз стучаться в почтовый
/// узел — свойство узла и наших соединений, а не практики. Поэтому арендатора
/// порт не спрашивает вовсе.
///
/// Негодная запись отвергается целиком доменом (`RetryPolicy::Compose`), и
/// продолжает действовать прежняя: половина настройки хуже старой целиком.
class DynamicConfigRetryPolicies final : public ports::RetryPolicies {
public:
    explicit DynamicConfigRetryPolicies(userver::dynamic_config::Source source);

    ~DynamicConfigRetryPolicies() override;

    core::Result<RetryPolicy> Current() const override;

    /// Сколько строк брать за проход. Не политика повторов, а размер прохода —
    /// поэтому отдельным вопросом, а не полем `RetryPolicy`: домену он не нужен
    /// вовсе, его спрашивает тот, кто зовёт сценарий.
    int Batch() const;

    /// Как часто просыпаться. Спрашивается компонентом при старте.
    core::Instant::Duration Poll() const;

private:
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::notifications
