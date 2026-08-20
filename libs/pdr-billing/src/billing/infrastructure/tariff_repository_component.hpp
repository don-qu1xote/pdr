#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "billing/application/ports/tariff_repository.hpp"
#include "billing/infrastructure/postgres_tariff_repository.hpp"

namespace pdr::billing {

/// Тонкая обёртка: компонент собирает адаптер из кластера Postgres и отдаёт
/// наружу ссылку на порт. Ничего доменного в нём нет и быть не должно — всё,
/// что можно проверить без сервиса, живёт в слоях ниже.
class TariffRepositoryComponent final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "tariff-repository";

    TariffRepositoryComponent(const userver::components::ComponentConfig& config,
                              const userver::components::ComponentContext& context);

    const ports::TariffRepository& Repository() const noexcept {
        return repository_;
    }

private:
    PostgresTariffRepository repository_;
};

}  // namespace pdr::billing
