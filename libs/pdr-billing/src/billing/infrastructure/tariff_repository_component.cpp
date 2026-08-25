#include "billing/infrastructure/tariff_repository_component.hpp"

#include <userver/storages/postgres/component.hpp>

namespace pdr::billing {
namespace {

/// Имя компонента базы в статическом конфиге сервиса.
constexpr std::string_view kPostgresComponent = "postgres-pdr";

}  // namespace

TariffRepositoryComponent::TariffRepositoryComponent(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      repository_{
          context.FindComponent<userver::components::Postgres>(kPostgresComponent).GetCluster()} {}

}  // namespace pdr::billing
