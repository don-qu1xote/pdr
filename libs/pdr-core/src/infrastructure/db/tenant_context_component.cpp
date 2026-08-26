#include "infrastructure/db/tenant_context_component.hpp"

#include <string>

#include <userver/components/component.hpp>
#include <userver/storages/postgres/component.hpp>

namespace pdr::infrastructure::db {
namespace {

/// Имя компонента базы в статическом конфиге сервиса.
constexpr std::string_view kPostgresComponent = "postgres-pdr";

}  // namespace

TenantContextComponent::TenantContextComponent(const userver::components::ComponentConfig& config,
                                               const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      context_{
          context.FindComponent<userver::components::Postgres>(kPostgresComponent).GetCluster()} {}

}  // namespace pdr::infrastructure::db
