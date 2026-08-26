#pragma once

#include "application/ports/id_generator.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Заведение участника в базе: человек и роль одной транзакцией.
///
/// Транзакция здесь не своя — она у области арендатора, и это ровно то, что
/// нужно: обе вставки идут в ней, и «наполовину заведён» не возникает даже при
/// оборванном соединении.
class PostgresParticipantDirectory final : public ports::ParticipantDirectory {
public:
    PostgresParticipantDirectory(infrastructure::db::ScopedTenantContext& scope,
                                 const application::ports::IdGenerator& ids) noexcept;

    core::Result<void> Enrol(const core::TenantId& tenant,
                             const ports::Enrolment& enrolment) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::identity
