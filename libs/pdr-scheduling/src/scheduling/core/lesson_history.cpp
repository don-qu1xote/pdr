#include "scheduling/core/lesson_history.hpp"

namespace pdr::scheduling {

std::string_view Name(LessonAction action) noexcept {
    switch (action) {
        case LessonAction::kBooked:
            return "booked";
        case LessonAction::kConfirmed:
            return "confirmed";
        case LessonAction::kRescheduled:
            return "rescheduled";
        case LessonAction::kCancelledByStudent:
            return "cancelled_by_student";
        case LessonAction::kCancelledByTutor:
            return "cancelled_by_tutor";
        case LessonAction::kHeld:
            return "held";
        case LessonAction::kNoShow:
            return "no_show";
        case LessonAction::kBoundary:
            break;
    }
    return "booked";
}

}  // namespace pdr::scheduling
