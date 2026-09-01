#pragma once

#include <optional>
#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "observability/contract.hpp"
#include "observability/infrastructure/dynamic_config_stream_settings.hpp"

namespace pdr::observability {

/// ПРОДУКТОВЫЙ ПОТОК: писать его теперь есть кому.
///
/// Контракт контекста, поднятый в процессе. Издатель — любой контекст — кладёт
/// запись и не знает ни про таблицу, ни про область арендатора: на каждую
/// запись здесь открывается своя область, потому что адаптер живёт внутри неё,
/// а компонент живёт всю жизнь сервиса.
///
/// Выключенный поток означает молчание, а не отказ: издателю нечего чинить, а
/// «писать ли поток вообще» — решение того, кто держит сервис.
class ProductEventsComponent final : public userver::components::ComponentBase, public Contract {
public:
    static constexpr std::string_view kName = "observability-product-events";

    ProductEventsComponent(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context);

    core::Result<void> Record(const core::TenantId& tenant,
                              std::string_view type,
                              int version,
                              Role actor,
                              core::Instant occurred_at,
                              Fields fields) override;

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::RandomIdGenerator ids_;
    std::optional<DynamicConfigStreamSettings> settings_;
};

}  // namespace pdr::observability
