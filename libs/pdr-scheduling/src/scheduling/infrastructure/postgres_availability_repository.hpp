#pragma once

#include <optional>

#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/availability_repository.hpp"

namespace pdr::scheduling {

/// Доступность репетитора в Postgres.
///
/// Записывается ЦЕЛИКОМ и внутри одной транзакции: снести и положить заново —
/// ровно та операция, которую делает репетитор на экране. Половины не бывает,
/// потому что область арендатора и есть транзакция.
class PostgresAvailabilityRepository final : public ports::AvailabilityRepository {
public:
    PostgresAvailabilityRepository(infrastructure::db::ScopedTenantContext& scope,
                                   const application::ports::IdGenerator& ids) noexcept;

    std::optional<Availability> Of(const core::TenantId& tenant,
                                   const core::PersonId& tutor) const override;

    core::Result<void> Replace(const core::TenantId& tenant,
                               const core::PersonId& tutor,
                               const Availability& availability) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::scheduling
