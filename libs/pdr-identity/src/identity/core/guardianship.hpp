#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

/// Опека: кто вправе действовать от имени ученика.
///
/// Отзыв — не удаление: связь была, и по ней принимались решения. Отозванная
/// опека остаётся с датой отзыва.
class Guardianship final {
public:
    static Guardianship Grant(core::TenantId tenant,
                              core::PersonId guardian,
                              core::PersonId student,
                              core::Instant granted_at);

    /// Собрать из хранилища — вместе с уже случившимся отзывом, если он был.
    static Guardianship Restore(core::TenantId tenant,
                                core::PersonId guardian,
                                core::PersonId student,
                                core::Instant granted_at,
                                std::optional<core::Instant> revoked_at);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Guardian() const noexcept {
        return guardian_;
    }
    const core::PersonId& Student() const noexcept {
        return student_;
    }
    core::Instant GrantedAt() const noexcept {
        return granted_at_;
    }
    std::optional<core::Instant> RevokedAt() const noexcept {
        return revoked_at_;
    }

    bool IsActive() const noexcept {
        return !revoked_at_.has_value();
    }

    /// Отозвать. Повторный отзыв — ожидаемый отказ, а не авария: опекун мог
    /// нажать дважды, и это не повод разбирать стек.
    core::Result<Guardianship> Revoked(core::Instant now) const;

private:
    Guardianship(core::TenantId tenant,
                 core::PersonId guardian,
                 core::PersonId student,
                 core::Instant granted_at,
                 std::optional<core::Instant> revoked_at);

    core::TenantId tenant_;
    core::PersonId guardian_;
    core::PersonId student_;
    core::Instant granted_at_;
    std::optional<core::Instant> revoked_at_;
};

}  // namespace pdr::identity
