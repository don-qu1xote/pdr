#include "identity/core/guardian_access.hpp"

#include "identity/core/age_status.hpp"

namespace pdr::identity {

core::Result<MaturityRule> MaturityRule::Compose(AgeThresholds thresholds,
                                                 core::Instant::Duration grace) {
    if (grace <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "maturity_grace_not_positive",
                           "окно нулевой длины — это мгновенный обрыв доступа"};
    }

    return MaturityRule{thresholds, grace};
}

GuardianAccess WeighConsents(std::span<const GuardianConsent> consents,
                             const std::optional<BirthDate>& student_born_on,
                             const MaturityRule& rule,
                             core::Instant now,
                             bool guardianship_holds) {
    GuardianScopeSet open;
    GuardianScopeSet deciding;
    GuardianScopeSet awaits;

    for (const auto& consent : consents) {
        if (!consent.IsActiveAt(now)) {
            continue;
        }
        if (consent.RestsOnGuardianship() && !guardianship_holds) {
            continue;
        }

        const auto scope = consent.Scope();
        if (consent.GrantedByStudent() || !student_born_on.has_value()) {
            open = open.With(scope);
            continue;
        }

        const auto grown_at = AgeStatus::TurnsAt(*student_born_on, rule.ThresholdYears(scope));
        if (now < grown_at) {
            open = open.With(scope);
            continue;
        }

        if (now < grown_at + rule.Grace()) {
            open = open.With(scope);
            deciding = deciding.With(scope);
            continue;
        }

        awaits = awaits.With(scope);
    }

    return GuardianAccess{open, deciding, awaits};
}

}  // namespace pdr::identity
