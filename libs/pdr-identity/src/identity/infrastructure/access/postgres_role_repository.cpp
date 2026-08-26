#include "identity/infrastructure/access/postgres_role_repository.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

namespace pdr::identity {
namespace {

const userver::storages::postgres::Query kRolesOf{
    "SELECT role FROM identity_role_assignment "
    "WHERE person_id = $1::uuid AND revoked_at IS NULL",
    userver::storages::postgres::Query::Name{"identity_role_assignment_of_person"},
};

}  // namespace

PostgresRoleRepository::PostgresRoleRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

RoleSet PostgresRoleRepository::RolesOf(const core::TenantId&, const core::PersonId& person) const {
    const auto result = scope_.Session().Execute(kRolesOf, person.ToString());

    RoleSet roles;
    for (const auto& row : result) {
        const auto stored = row["role"].As<std::string>();
        const auto role = ParseRole(stored);
        if (!role.has_value()) {
            throw std::runtime_error{"identity_role_assignment.role не роль: " + stored};
        }
        roles = roles.With(*role);
    }
    return roles;
}

}  // namespace pdr::identity
