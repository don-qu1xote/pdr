#include "scheduling/application/book_lesson.hpp"

#include "events/scheduling/lesson_booked.hpp"

namespace pdr::scheduling {

BookLesson::BookLesson(ports::LessonRepository& lessons,
                       const identity::Contract& identity,
                       const application::ports::Clock& clock,
                       const application::ports::IdGenerator& ids,
                       events::Bus& bus) noexcept
    : lessons_{lessons}, identity_{identity}, clock_{clock}, ids_{ids}, bus_{bus} {}

core::Result<core::LessonId> BookLesson::Execute(const Request& request) const {
    if (!identity_.MayActFor(request.tenant, request.actor, request.student)) {
        return core::Error{core::ErrorKind::kForbidden,
                           "not_allowed_to_act_for_student",
                           "записывать за ученика вправе он сам или его опекун"};
    }

    if (lessons_.FindAtSlot(request.tenant, request.tutor, request.starts_at).has_value()) {
        return core::Error{
            core::ErrorKind::kConflict, "slot_already_taken", "это время у репетитора уже занято"};
    }

    const auto now = clock_.Now();
    const auto lesson = Lesson::Schedule(ids_.Next<core::LessonId>(),
                                         request.tenant,
                                         request.tutor,
                                         {request.student},
                                         request.starts_at,
                                         request.duration,
                                         request.zone,
                                         now);
    if (!lesson.HasValue()) {
        return lesson.Failure();
    }

    const auto saved = lessons_.Save(lesson.Value());
    if (!saved.HasValue()) {
        return saved.Failure();
    }

    bus_.Publish(pdr::events::scheduling::LessonBooked{
        pdr::events::Envelope{request.tenant, now},
        lesson.Value().Id(),
        request.tutor,
        request.student,
        request.starts_at,
    });

    return lesson.Value().Id();
}

}  // namespace pdr::scheduling
