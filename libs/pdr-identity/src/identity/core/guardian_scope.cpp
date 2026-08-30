#include "identity/core/guardian_scope.hpp"

namespace pdr::identity {

std::string_view Name(GuardianScope scope) noexcept {
    switch (scope) {
        case GuardianScope::kSchedule:
            return "schedule";
        case GuardianScope::kPayments:
            return "payments";
        case GuardianScope::kNotesAndHomework:
            return "notes_and_homework";
        case GuardianScope::kRecordings:
            return "recordings";
        case GuardianScope::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<GuardianScope> ParseGuardianScope(std::string_view text) {
    for (const auto scope : kEveryGuardianScope) {
        if (Name(scope) == text) {
            return scope;
        }
    }
    return std::nullopt;
}

}  // namespace pdr::identity
