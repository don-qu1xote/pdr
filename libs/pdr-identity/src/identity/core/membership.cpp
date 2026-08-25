#include "identity/core/membership.hpp"

namespace pdr::identity {

std::string_view Name(Role role) noexcept {
    switch (role) {
        case Role::kOwner:
            return "owner";
        case Role::kTutor:
            return "tutor";
        case Role::kStudent:
            return "student";
        case Role::kGuardian:
            return "guardian";
    }
    return "student";
}

std::optional<Role> ParseRole(std::string_view text) {
    if (text == "owner") {
        return Role::kOwner;
    }
    if (text == "tutor") {
        return Role::kTutor;
    }
    if (text == "student") {
        return Role::kStudent;
    }
    if (text == "guardian") {
        return Role::kGuardian;
    }
    return std::nullopt;
}

TenantMembership TenantMembership::In(const Tenant& tenant, core::PersonId person, Role role) {
    return TenantMembership{tenant.Id(), std::move(person), role};
}

TenantMembership TenantMembership::Restore(core::TenantId tenant,
                                           core::PersonId person,
                                           Role role) {
    return TenantMembership{std::move(tenant), std::move(person), role};
}

}  // namespace pdr::identity
