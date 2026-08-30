#include "identity/core/independent_act.hpp"

namespace pdr::identity {

std::string_view Name(IndependentAct act) noexcept {
    switch (act) {
        case IndependentAct::kLessonRescheduled:
            return "lesson_rescheduled";
        case IndependentAct::kLessonCancelled:
            return "lesson_cancelled";
        case IndependentAct::kReviewWritten:
            return "review_written";
        case IndependentAct::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<IndependentAct> ParseIndependentAct(std::string_view text) {
    for (const auto act : kEveryIndependentAct) {
        if (Name(act) == text) {
            return act;
        }
    }
    return std::nullopt;
}

}  // namespace pdr::identity
