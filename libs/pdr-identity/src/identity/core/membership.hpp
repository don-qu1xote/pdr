#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>
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

/// Набор ролей одного человека в одном арендаторе.
///
/// НЕСКОЛЬКО РОЛЕЙ — НОРМА, а не краевой случай, и авторизации нужен именно
/// набор: репетитор, который у соседа родитель, приходит с двумя ролями сразу.
/// Отдельный тип, а не `std::set<Role>`: в наборе четыре значения, и хранить
/// ради них дерево — способ сделать проверку прав самой дорогой операцией
/// запроса.
class RoleSet final {
public:
    RoleSet() noexcept = default;

    static RoleSet Of(std::initializer_list<Role> roles) noexcept {
        RoleSet set;
        for (const auto role : roles) {
            set = set.With(role);
        }
        return set;
    }

    RoleSet With(Role role) const noexcept {
        RoleSet set;
        set.bits_ = static_cast<std::uint8_t>(bits_ | Bit(role));
        return set;
    }

    bool Has(Role role) const noexcept {
        return (bits_ & Bit(role)) != 0U;
    }

    bool Empty() const noexcept {
        return bits_ == 0U;
    }

    friend bool operator==(const RoleSet&, const RoleSet&) = default;

private:
    static std::uint8_t Bit(Role role) noexcept {
        return static_cast<std::uint8_t>(1U << static_cast<std::uint8_t>(role));
    }

    std::uint8_t bits_{0};
};

/// Все роли подряд — для обхода реестра прав.
inline constexpr std::array<Role, 4> kEveryRole{
    Role::kOwner,
    Role::kTutor,
    Role::kStudent,
    Role::kGuardian,
};

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
