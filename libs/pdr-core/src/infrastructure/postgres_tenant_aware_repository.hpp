#pragma once

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"

namespace pdr::infrastructure {

/// Единственное место во всём дереве, где арендатор объявляется базе.
///
/// Сессией области служит транзакция userver: репозитории контекстов получают
/// её готовой и уже привязанной к арендатору, поэтому «забыть установить
/// параметр» им нечем — параметра они не касаются вовсе.
///
/// Как и адаптеры контекстов, это обычный класс с обычным конструктором, а не
/// наследник components::ComponentBase: репозиторий, сросшийся с компонентом,
/// невозможно создать без поднятого сервиса, и любой тест превращается в
/// интеграционный.
class PostgresTenantAwareRepository final
    : public application::ports::TenantAwareRepository<userver::storages::postgres::Transaction> {
public:
    explicit PostgresTenantAwareRepository(userver::storages::postgres::ClusterPtr cluster);

private:
    void Run(const core::TenantId& tenant, const Work& work) override;

    userver::storages::postgres::ClusterPtr cluster_;
};

}  // namespace pdr::infrastructure
