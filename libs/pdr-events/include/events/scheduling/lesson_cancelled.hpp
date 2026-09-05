#pragma once

#include <cstdint>
#include <string_view>

#include "core/money.hpp"
#include "core/types/ids.hpp"
#include "events/envelope.hpp"

namespace pdr::events::scheduling {

/// Кто отменил. Не роль в кабинете, а СТОРОНА ОТМЕНЫ: от неё зависит удержание,
/// и подписчику она нужна не меньше самой суммы.
enum class CancelledBy : std::uint8_t {
    kStudent,
    kTutor,
};

constexpr std::string_view Name(CancelledBy by) noexcept {
    switch (by) {
        case CancelledBy::kStudent:
            return "student";
        case CancelledBy::kTutor:
            return "tutor";
    }
    return "student";
}

/// ПОЧЕМУ УДЕРЖАНО ИМЕННО СТОЛЬКО. Закрытый список, а не строка и не голое
/// число: сумма без причины — это то, что предъявляют человеку в споре, и
/// объяснить её тогда некому.
///
/// Список живёт ЗДЕСЬ, в общем реестре событий, а не в контексте расписания, и
/// это не мелочь: причину читает биллинг, а включать заголовок чужого контекста
/// ему нельзя. Расписание пользуется ей через свою зависимость от реестра —
/// направление одно, и цикла не возникает.
enum class RetentionReason : std::uint8_t {
    /// Отменили внутри окна бесплатной отмены. Не удержано ничего.
    kInsideFreeWindow,

    /// Отменил репетитор. Не удержано ничего НИКОГДА, какой бы ни была
    /// политика: удержать с ученика за то, что занятие отменил не он, —
    /// не строгость, а обман.
    kTutorCancelled,

    /// Отменили позже окна.
    kLateCancellation,

    /// Перенесли в пределах бесплатных переносов.
    kFreeReschedule,

    /// Перенесли позже окна и сверх бесплатных: считается поздней отменой.
    kLateReschedule,

    /// Никто не пришёл.
    kNoShow,

    /// Занятие состоялось: удержано всё, и это не удержание, а плата.
    kLessonHeld,
};

constexpr std::string_view Name(RetentionReason reason) noexcept {
    switch (reason) {
        case RetentionReason::kInsideFreeWindow:
            return "inside_free_window";
        case RetentionReason::kTutorCancelled:
            return "tutor_cancelled";
        case RetentionReason::kLateCancellation:
            return "late_cancellation";
        case RetentionReason::kFreeReschedule:
            return "free_reschedule";
        case RetentionReason::kLateReschedule:
            return "late_reschedule";
        case RetentionReason::kNoShow:
            return "no_show";
        case RetentionReason::kLessonHeld:
            return "lesson_held";
    }
    return "inside_free_window";
}

/// Занятие отменено, и вместе с ним посчитано удержание.
///
/// СУММА ПОСЧИТАНА ИЗДАТЕЛЕМ, А НЕ ПОДПИСЧИКОМ. Считает её расписание — по
/// политике тенанта и по часам, — потому что только оно знает, во сколько
/// занятие начиналось и когда пришла отмена. Биллинг получает готовое число и
/// повода спрашивать расписание у него нет: обращения к деньгам из расписания
/// нет ни одного, и обратного тоже.
struct LessonCancelled final {
    static constexpr std::string_view kType = "scheduling.lesson_cancelled";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::LessonId lesson;

    /// Кто нажал отмену. Идентификатор человека, а не роль: роль спрашивают у
    /// identity, а событие говорит фактом.
    core::PersonId actor;
    CancelledBy by{CancelledBy::kStudent};
    core::Money retained;
    RetentionReason reason{RetentionReason::kInsideFreeWindow};
};

}  // namespace pdr::events::scheduling
