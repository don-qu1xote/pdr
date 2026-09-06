#pragma once

#include <string_view>

#include "core/money.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"
#include "events/scheduling/lesson_cancelled.hpp"

namespace pdr::events::scheduling {

/// Занятие переехало. ТО ЖЕ САМОЕ ЗАНЯТИЕ, а не новое.
///
/// Идентификатор в событии один, и это не экономия поля: перенос, разложенный на
/// «отменили одно и записали другое», рвёт всё, что на занятие ссылалось, —
/// историю, оплату, материалы. Поэтому здесь `lesson` и два момента, старый и
/// новый, а не два идентификатора.
///
/// Удержание посчитано ИЗДАТЕЛЕМ, как и при отмене: перенос позже окна и сверх
/// бесплатных считается поздней отменой, и знает об этом расписание, а не
/// биллинг. Причина берётся из общего списка `RetentionReason` — он объявлен
/// рядом, в `lesson_cancelled.hpp`, и живёт в одном экземпляре: два списка
/// причин разошлись бы на первой же новой политике.
struct LessonRescheduled final {
    static constexpr std::string_view kType = "scheduling.lesson_rescheduled";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::LessonId lesson;

    /// Кого это касается — по той же причине, что и у отмены: подписчику надо
    /// написать обоим, а ходить за участниками в расписание ему нечем.
    core::PersonId tutor;
    core::PersonId student;

    /// Кто перенёс. Идентификатор человека, а не роль: роль спрашивают у
    /// identity, а событие говорит фактом.
    core::PersonId actor;
    core::Instant was;
    core::Instant becomes;
    core::Money retained;
    RetentionReason reason{RetentionReason::kFreeReschedule};
};

}  // namespace pdr::events::scheduling
