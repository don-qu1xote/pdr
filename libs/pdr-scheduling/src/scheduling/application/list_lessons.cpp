#include "scheduling/application/list_lessons.hpp"

#include "scheduling/core/recurrence.hpp"

namespace pdr::scheduling {

ListLessons::ListLessons(const ports::LessonRepository& lessons) noexcept : lessons_{lessons} {}

core::Result<std::vector<Lesson>> ListLessons::Execute(const Request& request) const {
    if (request.window.Length() > kDefaultHorizon) {
        return core::Error{core::ErrorKind::kValidation,
                           "schedule_window_over_horizon",
                           "за один раз расписание показывается не больше чем за год"};
    }

    if (request.side == Side::kTutor) {
        return lessons_.OfTutor(request.tenant, request.whose, request.window);
    }
    return lessons_.OfParticipant(request.tenant, request.whose, request.window);
}

}  // namespace pdr::scheduling
