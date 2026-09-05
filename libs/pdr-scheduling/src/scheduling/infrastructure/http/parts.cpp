#include "scheduling/infrastructure/http/parts.hpp"

#include <stdexcept>
#include <string>

#include <userver/components/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "infrastructure/db/tenant_context_component.hpp"
#include "infrastructure/http/dynamic_config_key_lifetime.hpp"
#include "infrastructure/http/operation.hpp"

namespace pdr::scheduling::http {
namespace {

pdr::http::KeyLifetime LifetimeFrom(const userver::components::ComponentContext& context) {
    const infrastructure::http::DynamicConfigKeyLifetime lifetimes{
        context.FindComponent<userver::components::DynamicConfig>().GetSource()};
    const auto lifetime = lifetimes.Lifetime();
    if (!lifetime.HasValue()) {
        throw std::runtime_error{"расписание: срок ключа повтора не годится: " +
                                 lifetime.Failure().Detail()};
    }
    return lifetime.Value();
}

}  // namespace

Parts::Parts(const userver::components::ComponentConfig& config,
             const userver::components::ComponentContext& context)
    : tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()},
      storage_{tenants_},
      callers_{context.FindComponent<infrastructure::http::Callers>(
          config["callers"].As<std::string>())},
      permissions_{
          context.FindComponent<identity::Contract>(config["permissions"].As<std::string>())},
      lifetime_{LifetimeFrom(context)} {}

userver::yaml_config::Schema Parts::Schema() {
    return userver::yaml_config::MergeSchemas<infrastructure::http::OperationComponent>(R"(
type: object
description: ручка расписания
additionalProperties: false
properties:
    permissions:
        type: string
        description: компонент, отвечающий на вопросы о правах
    callers:
        type: string
        description: компонент, опознающий пришедшего
)");
}

}  // namespace pdr::scheduling::http
