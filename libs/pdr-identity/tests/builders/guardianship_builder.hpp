#pragma once

#include <optional>
#include <utility>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/guardianship.hpp"

namespace pdr::identity::testing {

/// Билдер опеки: `GuardianshipBuilder{}.InTenant(t).Guardian(g).Student(s).Build()`.
///
/// Лежит в модуле identity, а не в libs/pdr-testing: платформенная оснастка не
/// имеет права зависеть от контекста (это проверяет scripts/check_layers.py), а
/// опека — доменное значение этого контекста. Доменные билдеры живут рядом со
/// своим доменом — docs/testing.md.
class GuardianshipBuilder final {
public:
    GuardianshipBuilder& InTenant(core::TenantId tenant) noexcept {
        tenant_ = tenant;
        return *this;
    }

    GuardianshipBuilder& Guardian(core::PersonId guardian) noexcept {
        guardian_ = guardian;
        return *this;
    }

    GuardianshipBuilder& Student(core::PersonId student) noexcept {
        student_ = student;
        return *this;
    }

    GuardianshipBuilder& GrantedAt(core::Instant granted_at) noexcept {
        granted_at_ = granted_at;
        return *this;
    }

    /// Уже отозванная связь: отзыв — не удаление, и тестам это нужно так же
    /// часто, как действующая опека.
    GuardianshipBuilder& RevokedAt(core::Instant revoked_at) noexcept {
        revoked_at_ = revoked_at;
        return *this;
    }

    /// Собирается всегда через `Restore`, даже для действующей связи: билдер
    /// строит заведомо годное значение, и разбирать здесь отказ было бы
    /// притворством. Отказы `Grant` проверяются своими случаями в тесте.
    Guardianship Build() const {
        return Guardianship::Restore(tenant_, guardian_, student_, granted_at_, revoked_at_);
    }

private:
    core::TenantId tenant_{pdr::testing::Numbered<core::TenantId>(1)};
    core::PersonId guardian_{pdr::testing::Numbered<core::PersonId>(10)};
    core::PersonId student_{pdr::testing::Numbered<core::PersonId>(20)};
    core::Instant granted_at_{pdr::testing::MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build()};
    std::optional<core::Instant> revoked_at_;
};

}  // namespace pdr::identity::testing
