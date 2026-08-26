#pragma once

#include "identity/application/ports/role_repository.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Роли в базе, `identity_role_assignment`.
///
/// Строится от области арендатора: пул в этом заголовке не упоминается вовсе, и
/// сходить за чужими ролями мимо арендатора нечем (`scripts/check_layers.py`).
///
/// Условия по арендатору в запросе нет — его ставит политика построчной защиты.
/// Условие `revoked_at is null` есть, и оно содержательное: отозванная роль
/// остаётся строкой с датой, но правами распоряжается действующая.
class PostgresRoleRepository final : public ports::RoleRepository {
public:
    explicit PostgresRoleRepository(infrastructure::db::ScopedTenantContext& scope) noexcept;

    RoleSet RolesOf(const core::TenantId& tenant, const core::PersonId& person) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
