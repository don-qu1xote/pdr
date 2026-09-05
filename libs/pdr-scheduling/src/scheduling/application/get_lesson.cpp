#include "scheduling/application/get_lesson.hpp"

#include <algorithm>
#include <utility>

namespace pdr::scheduling {
namespace {

core::Error NotFound() {
    return core::Error{core::ErrorKind::kNotFound, "lesson_not_found", "такого занятия здесь нет"};
}

bool InScheduleOf(const Lesson& lesson, const core::PersonId& whose, Side side) {
    if (side == Side::kTutor) {
        return lesson.Tutor() == whose;
    }
    const auto& people = lesson.Participants();
    return std::find(people.begin(), people.end(), whose) != people.end();
}

}  // namespace

GetLesson::GetLesson(const ports::LessonRepository& lessons) noexcept : lessons_{lessons} {}

core::Result<Lesson> GetLesson::Execute(const Request& request) const {
    auto found = lessons_.Find(request.tenant, request.lesson);
    if (!found.has_value() || !InScheduleOf(*found, request.whose, request.side)) {
        return NotFound();
    }
    return std::move(*found);
}

}  // namespace pdr::scheduling
