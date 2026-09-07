#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"

namespace pdr::events::notifications {

/// НАСТАЛ СРОК НАПОМНИТЬ, ЧТО ПЕРЕРЫВ КОНЧАЕТСЯ ЗАВТРА.
///
/// Издатель — `notifications`, как и у остальных напоминаний: что человека не
/// будет, знает расписание; что об этом напоминают за сутки до выхода, а не в
/// день выхода, — решение оповещений.
///
/// ЗАЧЕМ ЭТО ВООБЩЕ. Две недели без занятий стирают расписание из головы, и
/// первое утро после отпуска — это «а во сколько у меня сегодня?». Напоминание
/// приходит накануне и приносит расписание первой недели: не «вы выходите», а
/// «вот что у вас в понедельник и во вторник».
///
/// НА ШИНЕ ЭТОГО СОБЫТИЯ НЕ БЫВАЕТ — как и у остальных напоминаний. Оно
/// рождается сразу строкой очереди со сроком «сутки до конца перерыва» и лежит
/// там: база умеет ждать лучше, чем цикл по таблице.
///
/// Ключ намерения у строки один на перерыв, а не на занятие: сдвинули конец
/// отпуска — переехала та же строка, а не легла вторая.
struct ReminderBeforeReturn final {
    static constexpr std::string_view kType = "notifications.reminder_before_return";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::TimeOffId time_off;

    /// Кому напоминаем. Тому, у кого перерыв: расписание первой недели —
    /// его расписание.
    core::PersonId recipient;
    core::Instant returns_at;
};

}  // namespace pdr::events::notifications
