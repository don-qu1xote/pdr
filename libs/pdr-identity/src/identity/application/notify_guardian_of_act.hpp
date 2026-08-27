#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/core/independent_act.hpp"

namespace pdr::identity {

struct NotifyGuardianOfActRequest final {
    core::TenantId tenant;
    core::PersonId student;
    IndependentAct act;
};

/// Сообщить опекуну, что подопечный сделал это САМ.
///
/// ОБЯЗАТЕЛЬНО И НЕ ОТКЛЮЧАЕТСЯ. Ни настройкой, ни уровнем доступа, ни
/// согласием: у этого сценария нет ни одного условия, кроме «есть ли у ученика
/// опекун». Смысл возрастного порога — самостоятельность, а не тайна: подросток
/// переносит занятие без разрешения, а родитель знает об этом фактом, а не
/// расследованием, и узнаёт в тот же момент, а не из счёта в конце месяца.
///
/// Выключателя нет и завестись он не может незаметно: `check_guardian_notice.py`
/// роняет сборку, если в этом файле появится обращение к настройкам, к уровням
/// доступа или к согласиям.
///
/// ТЕКСТА ЗДЕСЬ НЕТ. Уходит код поступка из закрытого списка, а не то, что
/// ученик написал: опекун видит, что отзыв написан, и не видит, что в нём.
/// Место для текста в событии не предусмотрено, и это тоже проверяется.
///
/// Молчит в одном случае — когда опекунов нет вовсе. Взрослому
/// самостоятельному ученику сообщать некому, и это не ошибка.
class NotifyGuardianOfAct final {
public:
    NotifyGuardianOfAct(const ports::GuardianshipRepository& guardianships,
                        const application::ports::Clock& clock,
                        events::Bus& bus) noexcept;

    /// Сколько опекунов оповещено. Ноль — законный ответ.
    core::Result<int> Execute(const NotifyGuardianOfActRequest& request) const;

private:
    const ports::GuardianshipRepository& guardianships_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::identity
