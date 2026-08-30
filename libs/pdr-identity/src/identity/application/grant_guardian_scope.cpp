#include "identity/application/grant_guardian_scope.hpp"

#include "identity/core/age_status.hpp"

namespace pdr::identity {

GrantGuardianScope::GrantGuardianScope(const ports::GuardianshipRepository& guardianships,
                                       ports::GuardianConsents& consents,
                                       const ports::BirthDates& birth_dates,
                                       const ports::MaturitySettings& maturity,
                                       const application::ports::IdGenerator& ids,
                                       const application::ports::Clock& clock) noexcept
    : guardianships_{guardianships},
      consents_{consents},
      birth_dates_{birth_dates},
      maturity_{maturity},
      ids_{ids},
      clock_{clock} {}

core::Result<GuardianConsent> GrantGuardianScope::Execute(
    const GrantGuardianScopeRequest& request) const {
    const auto rule = maturity_.Rule();
    if (!rule) {
        return rule.Failure();
    }

    if (request.basis == ConsentBasis::kGuardianship) {
        if (!guardianships_.FindActive(request.tenant, request.guardian, request.student)
                 .has_value()) {
            return core::Error{core::ErrorKind::kNotFound,
                               "guardianship_not_found",
                               "действующей опеки между этими людьми нет"};
        }
    } else if (request.granted_by != request.student) {
        return core::Error{core::ErrorKind::kForbidden,
                           "watcher_named_by_someone_else",
                           "наблюдателя называет тот, за кем будут смотреть, и никто другой"};
    }

    if (consents_.FindActive(request.tenant, request.guardian, request.student, request.scope)
            .has_value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "consent_already_granted",
                           "этот уровень доступа уже открыт"};
    }

    const auto now = clock_.Now();
    const auto born_on = birth_dates_.Of(request.tenant, request.student);
    const bool student_decides =
        request.basis == ConsentBasis::kGuardianship && born_on.has_value() &&
        now >= AgeStatus::TurnsAt(*born_on, rule.Value().ThresholdYears(request.scope));

    if (student_decides && request.granted_by != request.student) {
        return core::Error{core::ErrorKind::kForbidden,
                           "consent_needs_student_word",
                           "ученик уже взрослый: этот уровень открывает только он сам"};
    }

    auto consent = GuardianConsent::Grant(ids_.Next<ConsentId>(),
                                          request.tenant,
                                          request.guardian,
                                          request.student,
                                          request.scope,
                                          request.basis,
                                          request.granted_by,
                                          now,
                                          request.expires_at);
    if (!consent) {
        return consent.Failure();
    }

    consents_.Save(consent.Value());
    return consent.Value();
}

}  // namespace pdr::identity
