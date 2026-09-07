#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/money.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "scheduling/application/ports/cancellation_policies.hpp"
#include "scheduling/application/ports/lesson_history.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/application/ports/local_days.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"
#include "scheduling/application/ports/time_off_repository.hpp"
#include "scheduling/core/time_off.hpp"

namespace pdr::scheduling {

/// СЦЕНАРИЙ ВТОРОГО ДЕЙСТВИЯ: ОДНО РЕШЕНИЕ НА ВСЕ ЗАНЯТИЯ ПЕРИОДА.
///
/// Разбор занятий поштучно в продукте есть — это обычная отмена и обычный
/// перенос. Но единственным путём он быть не может: двенадцать занятий за две
/// недели отпуска — это двенадцать одинаковых ответов на один и тот же вопрос,
/// и человек бросает на пятом, оставив семь занятий, которых не будет.
///
/// УДЕРЖАНИЕ НЕ НАЧИСЛЯЕТСЯ, КОГДА ОТМЕНЯЕТ РЕПЕТИТОР, — И ЗДЕСЬ ЭТОГО ПРАВИЛА
/// НЕТ НИ ОДНОЙ СТРОКОЙ. Оно уже есть в домене: у `Lesson::CancelByTutor`
/// политики нет в списке доводов, и обойти это нечем (PDR-SCHED-05). Сценарию
/// остаётся выбрать сторону, и выбирает он её не по роли и не по настройке, а
/// по самому занятию: ведёт его тот, у кого перерыв, — значит отменяет
/// репетитор. «Каникулы ученика — то же самое с другой стороны» получается из
/// этого само, без единой развилки по роли.
///
/// ОКНА БРОНИРОВАНИЯ ЗДЕСЬ НЕ СПРАШИВАЮТСЯ ВОВСЕ, и это отсутствующий довод, а
/// не забытый. Окно запрещает действие, политика его оценивает. Запретить
/// заболевшему репетитору отменить завтрашнее занятие — значит заставить его не
/// прийти молча, и ученик узнает об этом у закрытой двери. У ученика на
/// каникулах поздняя отмена по-прежнему стоит денег: платит политика, а не
/// запрет.
///
/// ЦЕНА ОДНА НА ВСЕ ЗАНЯТИЯ, как и у одиночной отмены (`CancelLesson`): «сколько
/// стоит занятие» — вопрос к биллингу, и задавать его отсюда нельзя. Кто зовёт
/// сценарий, тот и приносит цену.
class ResolveTimeOff final {
public:
    struct Request final {
        core::TenantId tenant;
        core::TimeOffId period;
        core::PersonId actor;

        TimeOffDecision decision{TimeOffDecision::kKeep};

        /// Что делать с сериями. Умолчание — пропуск: он меняет две даты, а
        /// сдвиг переписывает расписание до конца года у всех участников сразу.
        SeriesDecision series{SeriesDecision::kSkip};

        core::Money price;
    };

    struct Answer final {
        /// Сколько занятий попало в период.
        int lessons{};

        /// Сколько из них отменено и сколько перенесено. Оба числа, а не одно:
        /// решение одно на все, но сказать человеку надо, что именно вышло.
        int cancelled{};
        int postponed{};

        /// Сколько серий затронуто.
        int series{};

        friend bool operator==(const Answer&, const Answer&) = default;
    };

    ResolveTimeOff(ports::TimeOffRepository& periods,
                   ports::LessonRepository& lessons,
                   ports::LessonHistory& history,
                   ports::RecurrenceRepository& series,
                   const ports::CancellationPolicies& policies,
                   const ports::LocalDays& days,
                   const application::ports::IdGenerator& ids,
                   const application::ports::Clock& clock,
                   events::Bus& bus) noexcept;

    core::Result<Answer> Execute(const Request& request) const;

private:
    /// Серии — отдельным шагом: у них своё решение и свои две дороги.
    /// Возвращается, скольких серий это коснулось.
    core::Result<int> ApplyToSeries(const Request& request, const TimeOff& period) const;

    ports::TimeOffRepository& periods_;
    ports::LessonRepository& lessons_;
    ports::LessonHistory& history_;
    ports::RecurrenceRepository& series_;
    const ports::CancellationPolicies& policies_;
    const ports::LocalDays& days_;
    const application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::scheduling
