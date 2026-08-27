#include "identity/core/guardian_access.hpp"

#include "identity/core/age_status.hpp"

namespace pdr::identity {
namespace {

/// Возрастной порог ниже шести лет означает, что «взрослым» становится
/// первоклассник; выше двадцати одного — что взрослый самостоятельный ученик
/// годами не может решать за себя. Те же пределы стоят в схеме реестра, здесь
/// они на случай, когда значение пришло мимо неё.
constexpr int kLeastThreshold = 6;
constexpr int kMostThreshold = 21;

}  // namespace

core::Result<MaturityRule> MaturityRule::Compose(int threshold_years,
                                                 core::Instant::Duration grace) {
    if (threshold_years < kLeastThreshold || threshold_years > kMostThreshold) {
        return core::Error{core::ErrorKind::kValidation,
                           "maturity_threshold_out_of_range",
                           "порог самостоятельности вне разумного возраста"};
    }
    if (grace <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "maturity_grace_not_positive",
                           "окно нулевой длины — это мгновенный обрыв доступа"};
    }

    return MaturityRule{threshold_years, grace};
}

GuardianAccess WeighConsents(std::span<const GuardianConsent> consents,
                             const std::optional<BirthDate>& student_born_on,
                             const MaturityRule& rule,
                             core::Instant now) {
    GuardianScopeSet open;
    GuardianScopeSet deciding;
    GuardianScopeSet awaits;

    for (const auto& consent : consents) {
        if (!consent.IsActiveAt(now)) {
            continue;
        }

        const auto scope = consent.Scope();
        const bool student_already_spoke = consent.GrantedByStudent();
        if (!NeedsStudentWordWhenGrown(scope) || student_already_spoke ||
            !student_born_on.has_value()) {
            open = open.With(scope);
            continue;
        }

        const auto grown_at = AgeStatus::TurnsAt(*student_born_on, rule.ThresholdYears());
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
