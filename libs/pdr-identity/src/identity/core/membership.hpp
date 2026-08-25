#pragma once

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "core/types/ids.hpp"
#include "identity/core/tenant.hpp"

namespace pdr::identity {

/// Кем человек является в арендаторе. Список закрыт и совпадает со словами в
/// схеме — `identity_role_assignment_role_known`.
enum class Role : std::uint8_t {
    kOwner,
    kTutor,
    kStudent,
    kGuardian,
};

/// Код роли: то же слово, что в базе. Строкой роль в домене не бывает —
/// строку можно перепутать с текстом и написать в неё что угодно.
std::string_view Name(Role role) noexcept;

std::optional<Role> ParseRole(std::string_view text);

/// Участие человека в арендаторе: одна роль, одна запись.
///
/// НЕСКОЛЬКО РОЛЕЙ У ОДНОГО ЧЕЛОВЕКА В ОДНОМ АРЕНДАТОРЕ — НОРМА, а не краевой
/// случай: репетитор ведёт занятия в своём арендаторе и он же родитель ученика
/// у другого репетитора. Поэтому роль не поле человека, а отдельное значение:
/// сколько ролей, столько записей, и модель этому не мешает.
///
/// Участия без арендатора не существует, и это видно в конструкторе: `In`
/// принимает арендатора целиком, а не его идентификатор. Собрать участие
/// «просто так» нельзя — только внутри арендатора, который уже есть.
class TenantMembership final {
public:
    static TenantMembership In(const Tenant& tenant, core::PersonId person, Role role);

    /// Собрать из хранилища: там арендатор уже проверен внешним ключом.
    static TenantMembership Restore(core::TenantId tenant, core::PersonId person, Role role);

    const core::TenantId& TenantId() const noexcept {
        return tenant_;
    }
    const core::PersonId& Person() const noexcept {
        return person_;
    }
    Role InRole() const noexcept {
        return role_;
    }

    bool SameTenantAs(const TenantMembership& other) const noexcept {
        return tenant_ == other.tenant_;
    }

    friend bool operator==(const TenantMembership&, const TenantMembership&) = default;

private:
    TenantMembership(core::TenantId tenant, core::PersonId person, Role role) noexcept
        : tenant_{std::move(tenant)}, person_{std::move(person)}, role_{role} {}

    core::TenantId tenant_;
    core::PersonId person_;
    Role role_;
};

}  // namespace pdr::identity
