#include "observability/infrastructure/product_events_component.hpp"

#include <utility>

#include <userver/components/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>

#include "infrastructure/db/tenant_context_component.hpp"
#include "observability/application/contract_service.hpp"
#include "observability/infrastructure/postgres_product_event_stream.hpp"

namespace pdr::observability {

ProductEventsComponent::ProductEventsComponent(const userver::components::ComponentConfig& config,
                                               const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()},
      settings_{std::in_place,
                context.FindComponent<userver::components::DynamicConfig>().GetSource()} {}

core::Result<void> ProductEventsComponent::Record(const core::TenantId& tenant,
                                                  std::string_view type,
                                                  int version,
                                                  Role actor,
                                                  core::Instant occurred_at,
                                                  Fields fields) {
    if (!settings_->Enabled()) {
        return {};
    }

    auto scope = tenants_.Open(tenant,
                               userver::storages::postgres::ClusterHostType::kMaster,
                               userver::storages::postgres::TransactionOptions{});
    PostgresProductEventStream stream{scope, ids_};
    ContractService writing{stream};

    auto written = writing.Record(tenant, type, version, actor, occurred_at, std::move(fields));
    if (!written.HasValue()) {
        return written;
    }

    scope.Commit();
    return {};
}

}  // namespace pdr::observability
