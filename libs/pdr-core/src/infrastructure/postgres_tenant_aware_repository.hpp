#pragma once

#include <userver/storages/postgres/transaction.hpp>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::infrastructure {

/// Порт арендатора поверх области: сессией служит транзакция, в которой
/// арендатор уже объявлен.
///
/// Пула здесь нет и упоминать его нечем: единственная дверь к соединениям —
/// `db::TenantContext`, и она открывается только с арендатором. Это правило
/// проверяет `scripts/check_layers.py`.
///
/// Как и адаптеры контекстов, это обычный класс с обычным конструктором, а не
/// наследник components::ComponentBase: репозиторий, сросшийся с компонентом,
/// невозможно создать без поднятого сервиса, и любой тест превращается в
/// интеграционный.
class PostgresTenantAwareRepository final
    : public application::ports::TenantAwareRepository<userver::storages::postgres::Transaction> {
public:
    explicit PostgresTenantAwareRepository(db::TenantContext& context) noexcept;

private:
    void Run(const core::TenantId& tenant, const Work& work) override;

    db::TenantContext& context_;
};

}  // namespace pdr::infrastructure
