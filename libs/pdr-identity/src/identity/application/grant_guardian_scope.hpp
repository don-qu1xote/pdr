#pragma once

#include <optional>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/guardian_consents.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/maturity_settings.hpp"
#include "identity/core/guardian_consent.hpp"

namespace pdr::identity {

struct GrantGuardianScopeRequest final {
    core::TenantId tenant;
    core::PersonId guardian;
    core::PersonId student;
    GuardianScope scope;
    core::PersonId granted_by;
    std::optional<core::Instant> expires_at;
};

/// Открыть опекуну ОДИН уровень доступа.
///
/// По уровню за раз, а не «включить родителя»: единый флаг «родитель видит всё»
/// — это разговор, в котором никто не спрашивает про записи занятий отдельно,
/// а именно их и надо спрашивать отдельно.
///
/// ЭТОТ ЖЕ СЦЕНАРИЙ — ПОДТВЕРЖДЕНИЕ ПОВЗРОСЛЕВШЕГО УЧЕНИКА. Ученик, ставший
/// взрослым, открывает уровень сам, и `granted_by` оказывается им самим; после
/// этого правило совершеннолетия к этой строке не применяется — своё слово он
/// уже сказал. Отдельного сценария «подтвердить» нет намеренно: два способа
/// сказать «да» разошлись бы на первой правке.
///
/// Прав здесь не проверяют — это делает вызывающий через
/// `Contract::Decide(kManageGuardianAccess)`. Здесь только правила предметной
/// области, и главное из них: после совершеннолетия чувствительные уровни
/// открывает ТОЛЬКО сам ученик.
class GrantGuardianScope final {
public:
    GrantGuardianScope(const ports::GuardianshipRepository& guardianships,
                       ports::GuardianConsents& consents,
                       const ports::BirthDates& birth_dates,
                       const ports::MaturitySettings& maturity,
                       const application::ports::IdGenerator& ids,
                       const application::ports::Clock& clock) noexcept;

    core::Result<GuardianConsent> Execute(const GrantGuardianScopeRequest& request) const;

private:
    const ports::GuardianshipRepository& guardianships_;
    ports::GuardianConsents& consents_;
    const ports::BirthDates& birth_dates_;
    const ports::MaturitySettings& maturity_;
    const application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
