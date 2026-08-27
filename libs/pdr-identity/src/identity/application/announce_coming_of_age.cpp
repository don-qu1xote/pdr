#include "identity/application/announce_coming_of_age.hpp"

#include <chrono>
#include <optional>

#include "events/identity/capabilities_widened.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/capabilities.hpp"

namespace pdr::identity {
namespace {

constexpr auto kDay = std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24});

/// Порог, который человек перешёл ровно сейчас: раньше не дотягивал, теперь
/// дотянул. Порогов может совпасть несколько — если их сдвинули к одному
/// возрасту, — и тогда называется старший: он и есть новость.
std::optional<AgeThreshold> JustCrossed(const AgeStatus& before,
                                        const AgeStatus& now,
                                        const AgeThresholds& thresholds) {
    std::optional<AgeThreshold> crossed;
    for (const auto threshold : kEveryAgeThreshold) {
        const auto years = thresholds.Years(threshold);
        if (!before.Reached(years) && now.Reached(years)) {
            crossed = threshold;
        }
    }
    return crossed;
}

}  // namespace

AnnounceComingOfAge::AnnounceComingOfAge(const ports::GuardianshipRepository& guardianships,
                                         const ports::BirthDates& birth_dates,
                                         const ports::MaturitySettings& maturity,
                                         const application::ports::Clock& clock,
                                         events::Bus& bus) noexcept
    : guardianships_{guardianships},
      birth_dates_{birth_dates},
      maturity_{maturity},
      clock_{clock},
      bus_{bus} {}

core::Result<int> AnnounceComingOfAge::Execute(const core::TenantId& tenant,
                                               const core::PersonId& student) const {
    const auto rule = maturity_.Rule();
    if (!rule) {
        return rule.Failure();
    }

    const auto born_on = birth_dates_.Of(tenant, student);
    if (!born_on.has_value()) {
        return 0;
    }

    const auto now = clock_.Now();
    const auto today = AgeStatus::At(*born_on, now);
    const auto yesterday = AgeStatus::At(*born_on, now - kDay);
    if (!today || !yesterday) {
        return 0;
    }

    const auto& thresholds = rule.Value().Thresholds();
    const auto crossed = JustCrossed(yesterday.Value(), today.Value(), thresholds);
    if (!crossed.has_value()) {
        return 0;
    }

    const pdr::events::Envelope envelope{tenant, now};
    const auto code = Name(*crossed);
    const auto years = today.Value().Years();

    const auto guardians = guardianships_.GuardiansOf(tenant, student);
    if (guardians.empty()) {
        bus_.Publish(pdr::events::identity::CapabilitiesWidened{
            envelope, student, std::nullopt, code, years});
        return 1;
    }

    int announced = 0;
    for (const auto& guardian : guardians) {
        bus_.Publish(
            pdr::events::identity::CapabilitiesWidened{envelope, student, guardian, code, years});
        ++announced;
    }
    return announced;
}

}  // namespace pdr::identity
