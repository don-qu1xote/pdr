#pragma once

#include "core/types/ids.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity::ports {

/// Узкий порт: какие роли у человека в этом арендаторе.
///
/// Набор, а не одна роль: несколько ролей у одного человека — норма
/// (`TenantMembership`), и авторизации нужны все сразу.
///
/// Отозванные роли сюда не попадают: отзыв — это дата в строке, а не удаление,
/// но «была роль» и «есть роль» — разные вопросы, и правами распоряжается
/// второй.
class RoleRepository {
public:
    RoleRepository(const RoleRepository&) = delete;
    RoleRepository& operator=(const RoleRepository&) = delete;

    virtual ~RoleRepository() = default;

    virtual RoleSet RolesOf(const core::TenantId& tenant, const core::PersonId& person) const = 0;

protected:
    RoleRepository() = default;
};

}  // namespace pdr::identity::ports
