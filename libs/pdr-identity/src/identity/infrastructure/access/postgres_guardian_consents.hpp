#pragma once

#include <optional>
#include <vector>

#include "identity/application/ports/guardian_consents.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Согласия на уровни доступа опекуна в базе, `identity_guardian_consent`.
///
/// Строится от области арендатора: пул в этом заголовке не упоминается вовсе.
///
/// Наружу отдаются только действующие. Отозванные из таблицы не исчезают —
/// отзыв это дата, — но правами распоряжается действующее согласие, и порт,
/// умеющий отдать отозванное, рано или поздно отдал бы его туда, где решают.
class PostgresGuardianConsents final : public ports::GuardianConsents {
public:
    explicit PostgresGuardianConsents(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::vector<GuardianConsent> ActiveFor(const core::TenantId& tenant,
                                           const core::PersonId& guardian,
                                           const core::PersonId& student) const override;

    std::optional<GuardianConsent> FindActive(const core::TenantId& tenant,
                                              const core::PersonId& guardian,
                                              const core::PersonId& student,
                                              GuardianScope scope) const override;

    void Save(const GuardianConsent& consent) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
