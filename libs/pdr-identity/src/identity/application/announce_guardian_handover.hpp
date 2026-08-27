#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/guardian_consents.hpp"
#include "identity/application/ports/maturity_settings.hpp"

namespace pdr::identity {

/// Объявить, что у пары «опекун и ученик» пошёл срок на решение.
///
/// Сценарий отдельный, а не побочный эффект проверки прав: уведомление,
/// отправляемое из проверки, уходит на каждом обращении. Здесь оно уходит один
/// раз — по решению того, кто зовёт.
///
/// ЗВАТЬ ЕГО ПОКА НЕКОМУ. Обход учеников, у которых на этой неделе день
/// рождения, — периодическое задание, а заданий у контекста identity нет
/// (docs/architecture/first-service.md). Сценарий написан и проверен раньше
/// своего вызывающего сознательно: правило совершеннолетия работает уже сейчас,
/// а уведомление — то, что к нему добавится.
///
/// Молчит, когда объявлять нечего: срок не начался, уровней нет, ученик уже
/// сказал своё слово. Событие без повода хуже отсутствующего — на него
/// перестают смотреть.
class AnnounceGuardianHandover final {
public:
    AnnounceGuardianHandover(const ports::GuardianConsents& consents,
                             const ports::BirthDates& birth_dates,
                             const ports::MaturitySettings& maturity,
                             const application::ports::Clock& clock,
                             events::Bus& bus) noexcept;

    core::Result<bool> Execute(const core::TenantId& tenant,
                               const core::PersonId& guardian,
                               const core::PersonId& student) const;

private:
    const ports::GuardianConsents& consents_;
    const ports::BirthDates& birth_dates_;
    const ports::MaturitySettings& maturity_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::identity
