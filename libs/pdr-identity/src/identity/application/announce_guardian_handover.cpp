#include "identity/application/announce_guardian_handover.hpp"

#include <optional>

#include "events/identity/guardian_handover_started.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/guardian_access.hpp"

namespace pdr::identity {

AnnounceGuardianHandover::AnnounceGuardianHandover(const ports::GuardianConsents& consents,
                                                   const ports::BirthDates& birth_dates,
                                                   const ports::MaturitySettings& maturity,
                                                   const application::ports::Clock& clock,
                                                   events::Bus& bus) noexcept
    : consents_{consents},
      birth_dates_{birth_dates},
      maturity_{maturity},
      clock_{clock},
      bus_{bus} {}

core::Result<bool> AnnounceGuardianHandover::Execute(const core::TenantId& tenant,
                                                     const core::PersonId& guardian,
                                                     const core::PersonId& student) const {
    const auto rule = maturity_.Rule();
    if (!rule) {
        return rule.Failure();
    }

    const auto born_on = birth_dates_.Of(tenant, student);
    if (!born_on.has_value()) {
        return false;
    }

    const auto now = clock_.Now();
    const auto access =
        WeighConsents(consents_.ActiveFor(tenant, guardian, student), born_on, rule.Value(), now);
    if (access.Deciding().Empty()) {
        return false;
    }

    std::optional<core::Instant> decide_by;
    for (const auto scope : kEveryGuardianScope) {
        if (!access.Deciding().Has(scope)) {
            continue;
        }
        const auto closes_at =
            AgeStatus::TurnsAt(*born_on, rule.Value().ThresholdYears(scope)) + rule.Value().Grace();
        if (!decide_by.has_value() || closes_at < *decide_by) {
            decide_by = closes_at;
        }
    }

    bus_.Publish(pdr::events::identity::GuardianHandoverStarted{
        pdr::events::Envelope{tenant, now},
        guardian,
        student,
        *decide_by,
    });

    return true;
}

}  // namespace pdr::identity
