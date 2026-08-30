#include "identity/application/give_consent.hpp"

#include "identity/core/age_status.hpp"

namespace pdr::identity {

GiveConsent::GiveConsent(ports::Consents& consents,
                         const ports::PolicyVersions& versions,
                         const ports::GuardianshipRepository& guardianships,
                         const ports::BirthDates& birth_dates,
                         const ports::MaturitySettings& maturity,
                         application::ports::IdGenerator& ids,
                         const application::ports::Clock& clock) noexcept
    : consents_{consents},
      versions_{versions},
      guardianships_{guardianships},
      birth_dates_{birth_dates},
      maturity_{maturity},
      ids_{ids},
      clock_{clock} {}

core::Result<ConsentRecord> GiveConsent::Execute(const GiveConsentRequest& request) const {
    const auto now = clock_.Now();

    if (request.given_by != request.subject) {
        if (!guardianships_.FindActive(request.tenant, request.given_by, request.subject)
                 .has_value()) {
            return core::Error{core::ErrorKind::kForbidden,
                               "consent_not_yours_to_give",
                               "за ученика соглашается его опекун, и никто другой"};
        }
    } else {
        const auto rule = maturity_.Rule();
        if (!rule.HasValue()) {
            return rule.Failure();
        }

        const auto born_on = birth_dates_.Of(request.tenant, request.subject);
        if (born_on.has_value()) {
            const auto grown = AgeStatus::TurnsAt(
                *born_on, rule.Value().Thresholds().Years(AgeThreshold::kSlotsAndReviews));
            if (now < grown) {
                return core::Error{
                    core::ErrorKind::kForbidden,
                    "consent_needs_guardian",
                    "за ребёнка соглашается опекун: сам он этого сделать пока не может"};
            }
        }
    }

    auto record = ConsentRecord::Give(ids_.Next<ConsentRecordId>(),
                                      request.tenant,
                                      request.subject,
                                      request.given_by,
                                      request.kind,
                                      versions_.Current(),
                                      request.action,
                                      now);
    if (!record.HasValue()) {
        return record.Failure();
    }

    consents_.Save(record.Value());
    return record.Value();
}

}  // namespace pdr::identity
