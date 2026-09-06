#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"

namespace pdr::events::notifications {

/// НАСТАЛ СРОК НАПОМНИТЬ О ЗАВТРАШНЕМ ЗАНЯТИИ.
///
/// Издатель — `notifications`, и это не описка. Что занятие записано, знает
/// расписание; что о нём напоминают за сутки, а не за двое, — вопрос оповещений,
/// и расписание его не решает. Отсюда и имя типа: приставка называет того, чьё
/// это решение.
///
/// НА ШИНЕ ЭТОГО СОБЫТИЯ НЕ БЫВАЕТ. Оно рождается сразу строкой очереди со
/// сроком в будущем: занятие записали — строка легла на «за сутки до начала» и
/// лежит. Крон, который каждые пять минут перебирает все занятия и спрашивает
/// «не пора ли», не нужен вовсе — база умеет ждать лучше, чем цикл по таблице.
///
/// Перенос двигает эту же строку, отмена её убирает: ключ намерения у неё один
/// на занятие, и второго напоминания о том же занятии не появляется.
struct ReminderDayBefore final {
    static constexpr std::string_view kType = "notifications.reminder_day_before";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::LessonId lesson;

    /// Кому напоминаем. Ученик и репетитор получают своё письмо каждый: у них
    /// разные поводы прийти и разные последствия неявки.
    core::PersonId recipient;
    core::Instant starts_at;
};

}  // namespace pdr::events::notifications
