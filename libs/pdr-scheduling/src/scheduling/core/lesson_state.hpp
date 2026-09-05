#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "core/errors.hpp"

namespace pdr::scheduling {

/// Состояние занятия. Список закрытый, и свободного поля у состояния нет:
/// строка в этом месте означает, что через полгода в базе окажется «held »,
/// «HELD» и «проведено», а сравнивать их будет некому.
enum class LessonState : std::uint8_t {
    kPlanned,  ///< назначено, подтверждения ещё нет
    kConfirmed,  ///< подтверждено — обеими сторонами или правилом
    kHeld,       ///< состоялось
    kCancelled,  ///< отменено до начала
    kNoShow,     ///< никто не пришёл

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(LessonState state) noexcept;

inline constexpr std::array<LessonState, 5> kEveryLessonState{
    LessonState::kPlanned,
    LessonState::kConfirmed,
    LessonState::kHeld,
    LessonState::kCancelled,
    LessonState::kNoShow,
};

static_assert(kEveryLessonState.size() == static_cast<std::size_t>(LessonState::kBoundary),
              "состояние заведено, а в kEveryLessonState его нет: обход пропустит его, и "
              "«каждый недопустимый переход отклонён» станет непроверяемым");

/// Что случилось с занятием. Событие — не состояние: «отменить» можно из двух
/// состояний, и приводит оно в одно.
enum class LessonEvent : std::uint8_t {
    kConfirm,
    kHold,
    kCancel,
    kMarkNoShow,

    /// ПЕРЕНОС — СОБЫТИЕ, КОТОРОЕ НЕ МЕНЯЕТ СОСТОЯНИЯ, и это не странность.
    /// Занятие остаётся тем же и в том же состоянии; меняется его время.
    /// Событие нужно затем, чтобы «перенести проведённое» отклонялось той же
    /// машиной, что и «отменить проведённое», — а не отдельной проверкой,
    /// которая с ней разойдётся.
    kReschedule,

    /// ГРАНИЦА СПИСКА, а не событие.
    kBoundary,
};

std::string_view Name(LessonEvent event) noexcept;

inline constexpr std::array<LessonEvent, 5> kEveryLessonEvent{
    LessonEvent::kConfirm,
    LessonEvent::kHold,
    LessonEvent::kCancel,
    LessonEvent::kMarkNoShow,
    LessonEvent::kReschedule,
};

static_assert(kEveryLessonEvent.size() == static_cast<std::size_t>(LessonEvent::kBoundary),
              "событие заведено, а в kEveryLessonEvent его нет: обход по всем парам "
              "пропустит его, и недопустимый переход останется непроверенным");

/// МАШИНА СОСТОЯНИЙ ЗАНЯТИЯ — ЯВНАЯ И ОДНА.
///
/// Разрешено ровно девять переходов:
///
///     Planned   + Confirm    -> Confirmed
///     Planned   + Hold       -> Held
///     Planned   + Cancel     -> Cancelled
///     Planned   + MarkNoShow -> NoShow
///     Planned   + Reschedule -> Planned
///     Confirmed + Hold       -> Held
///     Confirmed + Cancel     -> Cancelled
///     Confirmed + MarkNoShow -> NoShow
///     Confirmed + Reschedule -> Confirmed
///
/// `Held`, `Cancelled` и `NoShow` — конечные: состоявшееся занятие не
/// отменяется задним числом, отменённое не проводится. Всё, чего нет в списке,
/// возвращает отказ `lesson_transition_not_allowed`, а не бросает исключение:
/// «нажали отмену на уже проведённом» — обычный ответ домена, а не авария.
core::Result<LessonState> Transition(LessonState from, LessonEvent event);

}  // namespace pdr::scheduling
