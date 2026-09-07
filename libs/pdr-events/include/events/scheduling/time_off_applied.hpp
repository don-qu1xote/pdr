#pragma once

#include <string_view>
#include <vector>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/envelope.hpp"
#include "events/scheduling/time_off.hpp"

namespace pdr::events::scheduling {

/// РЕШЕНИЕ ПО ПЕРЕРЫВУ ПРИМЕНЕНО КО ВСЕМ ЗАНЯТИЯМ СРАЗУ.
///
/// СОБЫТИЕ ОДНО НА ПЕРЕРЫВ, А НЕ ОДНО НА ЗАНЯТИЕ, и ради этого оно и заведено.
/// Каждое отменённое занятие издаёт свой `scheduling.lesson_cancelled` —
/// биллингу нужно именно поштучно, у него на каждое занятие свои деньги. А
/// человеку поштучно не нужно: двенадцать писем «занятие отменено» за одну
/// секунду — это не забота, это поломка на вид. Оповещения читают ЭТО событие и
/// пишут по письму на человека.
///
/// КОМУ СКАЗАТЬ — ПОЛЕ, А НЕ ДОГАДКА ПОДПИСЧИКА. Событие, из которого получателя
/// не видно, заставляет подписчика идти за ним в чужой контекст, и связь,
/// которой не должно быть, появляется. Список собирает издатель: он только что
/// прошёл по всем занятиям периода и знает всех поимённо.
struct TimeOffApplied final {
    static constexpr std::string_view kType = "scheduling.time_off_applied";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::TimeOffId time_off;

    /// Кого не будет.
    core::PersonId person;

    core::Instant from;
    core::Instant to;

    TimeOffDecision decision{TimeOffDecision::kKeep};

    /// Скольких занятий это коснулось. Числом, а не списком: подписчику нужно
    /// сказать «шесть занятий перенесены», а не перечислить их.
    int lessons{};

    /// КОМУ ПИСАТЬ — ПО ЧЕЛОВЕКУ, А НЕ ПО ЗАНЯТИЮ. Один и тот же ученик,
    /// потерявший четыре занятия, стоит здесь один раз. Сам виновник перерыва в
    /// списке тоже есть: он нажал кнопку и вправе увидеть, что из этого вышло.
    std::vector<core::PersonId> tell;
};

}  // namespace pdr::events::scheduling
