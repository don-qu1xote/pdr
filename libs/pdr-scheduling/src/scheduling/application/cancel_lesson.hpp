#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/money.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "scheduling/application/ports/cancellation_policies.hpp"
#include "scheduling/application/ports/lesson_history.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/cancellation_policy.hpp"

namespace pdr::scheduling {

/// Сценарий: отменить занятие и посчитать удержание.
///
/// БИЛЛИНГА ЗДЕСЬ НЕТ НИ ОДНОЙ СТРОКОЙ, и это видно по списку доводов: ни порта
/// денег, ни контракта биллинга среди них. Сумма считается доменом по политике
/// тенанта и уходит СОБЫТИЕМ; заберёт её тот, кому она нужна, и заберёт в своём
/// модуле. Так подсистемы остаются разделяемыми: расписание можно выкатить без
/// биллинга, а биллинг переписать, не тронув расписание.
///
/// ЦЕНА ПРИХОДИТ ПАРАМЕТРОМ, а не спрашивается у тарифа: «сколько стоит это
/// занятие» — вопрос к биллингу, и задать его отсюда было бы тем самым
/// обращением, которого быть не должно. Кто зовёт сценарий, тот и приносит
/// цену: у него она уже есть.
class CancelLesson final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId actor;
        core::LessonId lesson;

        /// Кто отменяет. Не выводится из актора: репетитор может отменять и за
        /// себя, и — в будущем — от имени практики, а сторона отмены решает
        /// удержание, и угадывать её нельзя.
        CancelledBy by{CancelledBy::kStudent};

        core::Money price;
    };

    CancelLesson(ports::LessonRepository& lessons,
                 ports::LessonHistory& history,
                 const ports::CancellationPolicies& policies,
                 const application::ports::Clock& clock,
                 events::Bus& bus) noexcept;

    core::Result<CancellationOutcome> Execute(const Request& request) const;

private:
    ports::LessonRepository& lessons_;
    ports::LessonHistory& history_;
    const ports::CancellationPolicies& policies_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::scheduling
