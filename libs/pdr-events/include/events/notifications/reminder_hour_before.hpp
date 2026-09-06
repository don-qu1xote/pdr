#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"

namespace pdr::events::notifications {

/// НАСТАЛ СРОК НАПОМНИТЬ, ЧТО ЗАНЯТИЕ ЧЕРЕЗ ЧАС.
///
/// Второе напоминание, а не то же самое с другим сроком. Разные типы затем, что
/// это разные письма: за сутки человек ещё может передоговориться, за час —
/// только прийти, и текст у них не совпадает ни одним словом.
///
/// Устроено так же, как и суточное: отложенная строка очереди, а не крон по
/// занятиям. Подробности — в `reminder_day_before.hpp`, повторять их здесь
/// незачем.
struct ReminderHourBefore final {
    static constexpr std::string_view kType = "notifications.reminder_hour_before";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::LessonId lesson;
    core::PersonId recipient;
    core::Instant starts_at;
};

}  // namespace pdr::events::notifications
