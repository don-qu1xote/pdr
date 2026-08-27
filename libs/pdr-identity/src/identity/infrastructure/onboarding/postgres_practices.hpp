#pragma once

#include <optional>

#include "identity/application/ports/practices.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Практики и их видимость поверх `identity_tenant`.
///
/// ОДНА ДВЕРЬ, как у всех: практика читается и пишется через область
/// арендатора. Общей очереди на разбор здесь нет — она работа оператора
/// (`db/practice/queue.sql`), потому что `identity_tenant` несёт арендатора, а
/// вторая дверь для таблиц с ним закрыта.
class PostgresPractices final : public ports::Practices {
public:
    explicit PostgresPractices(infrastructure::db::ScopedTenantContext& scope) noexcept;

    core::Result<void> Open(const Tenant& tenant,
                            const core::TimeZone& zone,
                            const Practice& practice) override;

    std::optional<Practice> Find(const core::TenantId& tenant) const override;

    void Save(const Practice& practice) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
