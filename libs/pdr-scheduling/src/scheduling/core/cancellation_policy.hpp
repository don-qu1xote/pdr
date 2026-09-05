#pragma once

#include <chrono>

#include "core/errors.hpp"
#include "core/money.hpp"
#include "core/percent.hpp"
#include "core/types/time.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "scheduling/core/lesson_state.hpp"

namespace pdr::scheduling {

/// Почему удержано именно столько. Тип берётся из реестра событий: причина
/// уходит в `scheduling.lesson_cancelled`, и второго перечисления с теми же
/// значениями заводить нельзя — они разойдутся в первый же день.
using RetentionReason = events::scheduling::RetentionReason;

using CancelledBy = events::scheduling::CancelledBy;

/// ПОЛИТИКА ОТМЕНЫ — ЗНАЧЕНИЕ, А НЕ КОНСТАНТА В КОДЕ.
///
/// Принадлежит тенанту и настраивается репетитором; умолчания приходят из
/// динамического конфига (`PDR_CANCELLATION_POLICY`), а не из этого файла.
/// Захардкоженное окно отмены — это спор с учеником, который нельзя решить, не
/// выкатив сборку.
///
/// ЧЕТЫРЕ ВЕЛИЧИНЫ, А НЕ ТРИ. Перенос — не «отмена плюс запись»: у него своя
/// мера снисхождения, и без неё «первый перенос бесплатен» выразить нечем.
/// Сколько переносов прощается, — такое же настраиваемое число, как и окно.
class CancellationPolicy final {
public:
    using Window = std::chrono::minutes;

    /// Собрать политику. Отказ — обычное значение: доля вне ноль-ста и
    /// отрицательное окно приходят из настройки, которую заполнил человек.
    static core::Result<CancellationPolicy> Compose(Window free_window,
                                                    core::Percent late_retention,
                                                    core::Percent no_show_retention,
                                                    int free_reschedules);

    /// До какого момента отмена бесплатна. Отсчитывается ОТ НАЧАЛА ЗАНЯТИЯ
    /// назад: «за сутки» — это про занятие, а не про календарь.
    Window FreeWindow() const noexcept {
        return free_window_;
    }
    core::Percent LateRetention() const noexcept {
        return late_retention_;
    }
    core::Percent NoShowRetention() const noexcept {
        return no_show_retention_;
    }
    int FreeReschedules() const noexcept {
        return free_reschedules_;
    }

    /// Внутри ли окна отмена, пришедшая в этот момент.
    ///
    /// ГРАНИЦА ВКЛЮЧЕНА В ОКНО: отмена ровно за сутки — бесплатна. Иначе
    /// человеку, нажавшему кнопку секунда в секунду, приходится объяснять, что
    /// «за сутки» значило «за сутки и одну секунду».
    bool Free(core::Instant starts_at, core::Instant now) const noexcept;

    friend bool operator==(const CancellationPolicy&, const CancellationPolicy&) = default;

private:
    CancellationPolicy(Window free_window,
                       core::Percent late_retention,
                       core::Percent no_show_retention,
                       int free_reschedules) noexcept
        : free_window_{free_window},
          late_retention_{late_retention},
          no_show_retention_{no_show_retention},
          free_reschedules_{free_reschedules} {}

    Window free_window_{};
    core::Percent late_retention_{core::Percent::Nothing()};
    core::Percent no_show_retention_{core::Percent::Nothing()};
    int free_reschedules_{0};
};

/// ЧЕМ КОНЧИЛАСЬ ОПЕРАЦИЯ: новое состояние, посчитанная сумма и причина.
///
/// Сумма считается ЗДЕСЬ, а не в биллинге, и это не нарушение границы: удержание
/// зависит от времени отмены и политики тенанта, а их знает расписание.
/// Биллинг получает готовое число событием — обращения к деньгам из расписания
/// нет ни одного.
struct CancellationOutcome final {
    LessonState new_state{LessonState::kPlanned};
    core::Money retained;
    RetentionReason reason{RetentionReason::kInsideFreeWindow};

    friend bool operator==(const CancellationOutcome&, const CancellationOutcome&) = default;
};

}  // namespace pdr::scheduling
