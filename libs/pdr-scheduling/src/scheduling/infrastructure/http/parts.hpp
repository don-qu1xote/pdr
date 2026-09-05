#pragma once

#include <optional>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/yaml_config/schema.hpp>

#include "core/idempotency.hpp"
#include "events/in_memory_bus.hpp"
#include "identity/contract.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/postgres_idempotency_keys.hpp"
#include "infrastructure/postgres_tenant_aware_repository.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "infrastructure/userver_clock.hpp"

namespace pdr::scheduling::http {

/// Всё, из чего собирается ручка расписания, — собранное ОДИН РАЗ.
///
/// Шесть компонентов-операций отличаются друг от друга ручкой и именем, а не
/// набором зависимостей: дверь к базе, права, ключи повтора, часы, генератор
/// идентификаторов и шина событий нужны им одинаково. Шесть копий этой сборки
/// разошлись бы на первой же правке — одна взяла бы часы из другого места.
///
/// ШИНА СОБЫТИЙ ЖИВЁТ ЗДЕСЬ И ПОКА НИКОГО НЕ ЗОВЁТ. Издатель не знает
/// подписчиков — в этом весь смысл шины, — а подписчик добавляется в своём
/// модуле; в этот процесс не добавлен ни один, и опубликованное событие никуда
/// не идёт. Издателя это не касается: он опубликовал.
///
/// ЧУЖИЕ КОМПОНЕНТЫ НАЗЫВАЕТ КОНФИГ, А НЕ ЭТОТ ФАЙЛ. Права и опознание
/// пришедшего поднимает контекст identity, и заголовков его сборки здесь нет:
/// расписание знает `identity::Contract` — публичный контракт — и `Callers` —
/// платформенный порт. Под какими именами эти компоненты подняты, решает тот,
/// кто собирает процесс, а не тот, кто спрашивает. Правило проверяет
/// scripts/check_layers.py.
class Parts final {
public:
    Parts(const userver::components::ComponentConfig& config,
          const userver::components::ComponentContext& context);

    /// Схема статического конфига — общая на все операции расписания: спрашивают
    /// они одно и то же.
    static userver::yaml_config::Schema Schema();

    infrastructure::PostgresTenantAwareRepository& Database() noexcept {
        return storage_;
    }
    infrastructure::http::PostgresIdempotencyKeys& Keys() noexcept {
        return keys_;
    }
    const infrastructure::http::Callers& Callers() const noexcept {
        return callers_;
    }
    const identity::Contract& Permissions() const noexcept {
        return permissions_;
    }
    const application::ports::Clock& Clock() const noexcept {
        return clock_;
    }
    const application::ports::IdGenerator& Ids() const noexcept {
        return ids_;
    }
    events::Bus& Bus() noexcept {
        return bus_;
    }
    pdr::http::KeyLifetime Lifetime() const noexcept {
        return lifetime_;
    }

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::PostgresTenantAwareRepository storage_;
    infrastructure::http::PostgresIdempotencyKeys keys_;
    infrastructure::UserverClock clock_;
    infrastructure::RandomIdGenerator ids_;
    events::InMemoryBus bus_;
    const infrastructure::http::Callers& callers_;
    const identity::Contract& permissions_;
    pdr::http::KeyLifetime lifetime_;
};

}  // namespace pdr::scheduling::http
