#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"

namespace pdr::events::scheduling {

/// Занятие записано в слот.
///
/// Издатель — контекст scheduling. По этому событию списывают занятие из
/// пакета, шлют напоминание, считают загрузку — каждый в своём модуле, и ни
/// один из них не будет упомянут в коде scheduling.
struct LessonBooked final {
    static constexpr std::string_view kType = "scheduling.lesson_booked";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::LessonId lesson;
    core::PersonId tutor;
    core::PersonId student;
    core::Instant starts_at;
};

}  // namespace pdr::events::scheduling
