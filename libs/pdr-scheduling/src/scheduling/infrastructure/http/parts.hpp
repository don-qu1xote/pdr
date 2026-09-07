#pragma once

#include <optional>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/yaml_config/schema.hpp>

#include "core/idempotency.hpp"
#include "events/listeners.hpp"
#include "identity/contract.hpp"
#include "infrastructure/crypto_secret_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/postgres_idempotency_keys.hpp"
#include "infrastructure/postgres_tenant_aware_repository.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "infrastructure/sha256_digests.hpp"
#include "infrastructure/userver_clock.hpp"
#include "scheduling/infrastructure/dynamic_config_booking_windows.hpp"
#include "scheduling/infrastructure/dynamic_config_calendar_wording.hpp"

namespace pdr::scheduling::http {

/// Всё, из чего собирается ручка расписания, — собранное ОДИН РАЗ.
///
/// Шесть компонентов-операций отличаются друг от друга ручкой и именем, а не
/// набором зависимостей: дверь к базе, права, ключи повтора, часы, генератор
/// идентификаторов и шина событий нужны им одинаково. Шесть копий этой сборки
/// разошлись бы на первой же правке — одна взяла бы часы из другого места.
///
/// ШИНА СОБЫТИЙ ЗАВОДИТСЯ НА КАЖДОЕ ОБРАЩЕНИЕ, А НЕ ЛЕЖИТ ЗДЕСЬ.
///
/// Подписчик обязан писать в ТУ ЖЕ транзакцию, в которой меняется занятие:
/// строка исходящей очереди и само занятие коммитятся вместе или не коммитятся
/// вовсе. Общая на процесс шина этого не умеет — её подписчику пришлось бы
/// откуда-то взять транзакцию, а взять её неоткуда. Поэтому здесь лежит не
/// шина, а тот, кто её собирает: `events::Listeners`.
///
/// Издатель по-прежнему не знает подписчиков — в этом весь смысл шины. Он видит
/// платформенный порт; кто на самом деле сидит на шине, решает статический
/// конфиг процесса, и расписание не включает ни одного заголовка того контекста.
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

    /// НЕПРЕДСКАЗУЕМАЯ случайность — отдельным портом от обычного генератора.
    /// Ссылка на календарь живёт в чужих настройках годами, и предсказуемая
    /// отдаёт чужое расписание молча; подставить сюда `IdGenerator` не даёт
    /// компилятор.
    const application::ports::SecretGenerator& Secrets() const noexcept {
        return secrets_;
    }
    const ports::BookingWindowDefaults& Windows() const noexcept {
        return windows_;
    }
    const application::ports::Digests& Digests() const noexcept {
        return digests_;
    }
    CalendarWording Words() const {
        return wording_.Words();
    }
    const events::Listeners<infrastructure::db::ScopedTenantContext>& Listeners() const noexcept {
        return listeners_;
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
    infrastructure::CryptoSecretGenerator secrets_;
    DynamicConfigBookingWindows windows_;
    DynamicConfigCalendarWording wording_;
    infrastructure::Sha256Digests digests_;
    const events::Listeners<infrastructure::db::ScopedTenantContext>& listeners_;
    const infrastructure::http::Callers& callers_;
    const identity::Contract& permissions_;
    pdr::http::KeyLifetime lifetime_;
};

}  // namespace pdr::scheduling::http
