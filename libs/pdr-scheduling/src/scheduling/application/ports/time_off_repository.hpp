#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/time_off.hpp"

namespace pdr::scheduling::ports {

/// Перерывы: завести, прочитать, записать решение.
///
/// ДВА ДЕЙСТВИЯ — ДВА МЕТОДА, и это прямое отражение сценария. Человек сначала
/// говорит «меня не будет с первого по четырнадцатое» и только потом — что
/// делать с занятиями: список занятий он до первого действия не видел, и
/// решать ему было не о чем. Порт, у которого заведение и решение слиты в один
/// вызов, заставил бы клиента присылать решение вслепую.
///
/// РЕШЕНИЕ ПИШЕТСЯ, А НЕ ВЫВОДИТСЯ ИЗ ЗАНЯТИЙ. «Что мы сделали с расписанием»
/// через полгода не восстанавливается по самим занятиям: отменённое по отпуску
/// и отменённое поштучно выглядят одинаково, а спрашивают об этом по-разному.
class TimeOffRepository {
public:
    TimeOffRepository(const TimeOffRepository&) = delete;
    TimeOffRepository& operator=(const TimeOffRepository&) = delete;

    virtual ~TimeOffRepository() = default;

    /// Завести перерыв.
    ///
    /// ОТКАЗАТЬ МОЖЕТ, И ЭТО ЧАСТЬ ПОРТА: два перерыва у одного человека не
    /// накладываются, и ловит это ограничение базы
    /// (`scheduling_time_off_no_overlap`). «Я в отпуске и одновременно болею» —
    /// не два периода, а один, и решение по занятиям у него одно.
    ///
    /// Фейк обязан отказывать в том же случае: иначе unit-прогон зелен на
    /// поведении, которого в проде нет.
    ///
    /// КОГДА ЗАВЕЛИ — ОТДЕЛЬНЫМ ДОВОДОМ, А НЕ ПОЛЕМ ПЕРИОДА. Момент заведения —
    /// запись о том, как всё было, а не часть самого перерыва: период «с
    /// первого по четырнадцатое» остаётся тем же, когда бы его ни завели. И
    /// главное — так `TimeOff::Compose` по-прежнему не получает «сейчас» ни в
    /// каком виде, а значит запретить период задним числом ему нечем.
    virtual core::Result<void> Save(const TimeOff& period, core::Instant declared_at) = 0;

    virtual std::optional<TimeOff> Find(const core::TenantId& tenant,
                                        const core::TimeOffId& id) const = 0;

    /// Записать принятое решение: одно на занятия, одно на серии.
    ///
    /// Второй раз по тому же перерыву — отказ: занятия уже отменены или
    /// перенесены, и «примените ещё раз, но по-другому» означало бы отменять
    /// отменённое. Передумал — это обычная запись и обычный перенос, поштучно.
    virtual core::Result<void> Decide(const core::TenantId& tenant,
                                      const core::TimeOffId& id,
                                      TimeOffDecision lessons,
                                      SeriesDecision series,
                                      core::Instant at) = 0;

protected:
    TimeOffRepository() = default;
};

}  // namespace pdr::scheduling::ports
