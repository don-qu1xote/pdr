#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "identity/application/policies/policy_set.hpp"
#include "identity/application/ports/configuration_faults.hpp"
#include "identity/application/ports/role_repository.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity::testing {

/// Роли в памяти: столько, сколько нужно тесту прав.
class FakeRoles final : public ports::RoleRepository {
public:
    void Grant(const core::TenantId& tenant, const core::PersonId& person, Role role) {
        auto& set = rows_[Key(tenant, person)];
        set = set.With(role);
    }

    RoleSet RolesOf(const core::TenantId& tenant, const core::PersonId& person) const override {
        const auto found = rows_.find(Key(tenant, person));
        return found == rows_.end() ? RoleSet{} : found->second;
    }

private:
    static std::string Key(const core::TenantId& tenant, const core::PersonId& person) {
        return tenant.ToString() + "|" + person.ToString();
    }

    std::unordered_map<std::string, RoleSet> rows_;
};

/// Поломки настройки, о которых можно спросить: что именно сообщили и сколько раз.
class FakeFaults final : public ports::ConfigurationFaults {
public:
    void NoPolicyFor(Action action) const override {
        reported_.push_back(action);
    }

    const std::vector<Action>& Reported() const noexcept {
        return reported_;
    }

private:
    mutable std::vector<Action> reported_;
};

}  // namespace pdr::identity::testing
