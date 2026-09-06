#include "scheduling/application/book_lesson.hpp"

#include "events/scheduling/lesson_booked.hpp"
#include "scheduling/core/participation.hpp"

namespace pdr::scheduling {

BookLesson::BookLesson(ports::LessonRepository& lessons,
                       const WindowsInForce& windows,
                       const application::ports::Clock& clock,
                       const application::ports::IdGenerator& ids,
                       events::Bus& bus) noexcept
    : lessons_{lessons}, windows_{windows}, clock_{clock}, ids_{ids}, bus_{bus} {}

core::Result<Lesson> BookLesson::Execute(const Request& request) const {
    const auto now = clock_.Now();

    const auto windows = windows_.For(request.tenant, request.tutor, request.student);
    if (!windows.HasValue()) {
        return windows.Failure();
    }

    const auto side =
        request.actor == request.tutor ? BookingSide::kScheduleOwner : BookingSide::kIncoming;
    const auto allowed =
        Allows(windows.Value(), BookingAction::kBook, side, request.starts_at, now);
    if (!allowed.HasValue()) {
        return allowed.Failure();
    }

    if (lessons_.FindAtSlot(request.tenant, request.tutor, request.starts_at).has_value()) {
        return core::Error{
            core::ErrorKind::kConflict, "slot_already_taken", "это время у репетитора уже занято"};
    }

    const auto lesson = Lesson::Schedule(ids_.Next<core::LessonId>(),
                                         request.tenant,
                                         request.tutor,
                                         {Participation::Joined(request.student)},
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

    return lesson.Value();
}

}  // namespace pdr::scheduling
