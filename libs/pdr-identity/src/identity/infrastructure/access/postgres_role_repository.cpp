#include "identity/infrastructure/access/postgres_role_repository.hpp"

#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

namespace pdr::identity {
PostgresRoleRepository::PostgresRoleRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

RoleSet PostgresRoleRepository::RolesOf(const core::TenantId&, const core::PersonId& person) const {
    const auto result =
        scope_.Session().Execute(sql::kIdentityRoleAssignmentOfPerson, person.ToString());

    RoleSet roles;
    for (const auto& stored : result.AsSetOf<std::string>(userver::storages::postgres::kFieldTag)) {
        const auto role = ParseRole(stored);
        if (!role.has_value()) {
            throw std::runtime_error{"identity_role_assignment.role не роль: " + stored};
        }
        roles = roles.With(*role);
    }
    return roles;
}

}  // namespace pdr::identity
