#pragma once

#include "application/ports/tenant_aware_repository.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::infrastructure {

/// Порт арендатора поверх области: сессией служит САМА ОБЛАСТЬ.
///
/// Не голая транзакция: у области спрашивают арендатора, которого она
/// объявила, а у транзакции — нечем. Работа, получившая транзакцию, знает
/// «где писать» и не знает «от чьего имени», и первый же адаптер, которому
/// арендатор понадобился, взял бы его откуда-нибудь ещё.
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
    : public application::ports::TenantAwareRepository<db::ScopedTenantContext> {
public:
    explicit PostgresTenantAwareRepository(db::TenantContext& context) noexcept;

private:
    void Run(application::ports::Intent intent,
             const core::TenantId& tenant,
             const Work& work) override;

    db::TenantContext& context_;
};

}  // namespace pdr::infrastructure
