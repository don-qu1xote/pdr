#pragma once

#include <optional>
#include <vector>

#include "application/ports/id_generator.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Опека в таблице `identity_guardianship`.
///
/// Отзыв — правка строки, а не новая рядом: частичный уникальный индекс
/// `identity_guardianship_active` не позволил бы двум действующим опекам на
/// пару, и правильно делает. Поэтому сохранение сначала пробует поправить
/// действующую строку и только потом заводит новую.
class PostgresGuardianships final : public ports::GuardianshipRepository {
public:
    PostgresGuardianships(infrastructure::db::ScopedTenantContext& scope,
                          const application::ports::IdGenerator& ids) noexcept;

    std::optional<Guardianship> FindActive(const core::TenantId& tenant,
                                           const core::PersonId& guardian,
                                           const core::PersonId& student) const override;

    std::vector<core::PersonId> GuardiansOf(const core::TenantId& tenant,
                                            const core::PersonId& student) const override;

    void Save(const Guardianship& guardianship) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::identity
