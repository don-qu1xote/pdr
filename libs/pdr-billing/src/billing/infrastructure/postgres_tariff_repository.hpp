#pragma once

#include <optional>

#include <userver/storages/postgres/cluster.hpp>

#include "billing/application/ports/tariff_repository.hpp"
#include "billing/core/tariff.hpp"

namespace pdr::billing {

/// Адаптер порта поверх Postgres — обычный класс с обычным конструктором.
///
/// Он НЕ наследует components::ComponentBase, и это главное решение этого файла.
/// Репозиторий, сросшийся с компонентом userver, невозможно создать без
/// поднятого сервиса: тогда любой тест сценария превращается в интеграционный,
/// с реальным бинарником и реальной базой. Компонент — это способ создать
/// адаптер, а не сам адаптер; он лежит рядом и умеет ровно это.
class PostgresTariffRepository final : public ports::TariffRepository {
public:
    explicit PostgresTariffRepository(userver::storages::postgres::ClusterPtr cluster);

    std::optional<Tariff> FindByCode(const TariffCode& code) const override;

private:
    userver::storages::postgres::ClusterPtr cluster_;
};

}  // namespace pdr::billing
