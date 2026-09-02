#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "core/errors.hpp"
#include "core/idempotency.hpp"

namespace pdr::infrastructure::http {

/// Срок жизни ключа повтора из динамического конфига.
///
/// Значение живёт не константой: срок подбирают под то, как быстро клиенты
/// повторяют оборвавшийся запрос, и менять его передеплоем — значит не менять
/// его никогда.
///
/// СТРУКТУРЫ ЗНАЧЕНИЯ ЗДЕСЬ НЕТ: она порождена из `PDR_IDEMPOTENCY` в
/// configs/dynamic/registry.yaml вместе с ключом, умолчанием и пределами
/// (`dynamic_config/variables/PDR_IDEMPOTENCY.hpp`). Разойтись схеме и коду
/// нечем — код и есть схема.
///
/// Пределы одного поля задаёт схема, а домен (`pdr::http::KeyLifetime::Compose`)
/// отвечает на другой вопрос — годится ли значение как срок; негодное
/// отвергается целиком, прежний срок продолжает действовать.
class DynamicConfigKeyLifetime final {
public:
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
