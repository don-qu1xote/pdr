#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/maturity_settings.hpp"

namespace pdr::identity {

/// Объявить, что ученик перешёл порог и права пришли сами.
///
/// ПЕРЕХОД АВТОМАТИЧЕСКИЙ, А СООБЩЕНИЕ — НЕТ. Права считаются из возраста на
/// каждый вопрос и меняются в полночь дня рождения без чьего-либо участия; но
/// узнать об этом обе стороны могут только из сообщения, иначе первым признаком
/// перехода окажется кнопка, которая вдруг перестала работать у родителя.
///
/// Сравниваются два момента — «вчера» и «сейчас», — и событие уходит, только
/// если между ними прибавилась хоть одна возможность. Событие без повода хуже
/// отсутствующего: на такие перестают смотреть.
///
/// ЗВАТЬ ЕГО ПОКА НЕКОМУ. Обход учеников, у которых сегодня день рождения, —
/// периодическое задание, а заданий у контекста identity нет
/// (docs/architecture/first-service.md). Сценарий написан и проверен раньше
/// своего вызывающего сознательно: правило работает уже сейчас, а рассылка —
/// то, что к нему добавится.
///
/// Часы — порт. Иначе проверка «день до, сам день, день после» превратилась бы
/// в ожидание дня рождения.
class AnnounceComingOfAge final {
public:
    AnnounceComingOfAge(const ports::GuardianshipRepository& guardianships,
                        const ports::BirthDates& birth_dates,
                        const ports::MaturitySettings& maturity,
                        const application::ports::Clock& clock,
                        events::Bus& bus) noexcept;

    /// Сколько событий ушло. Ноль — законный ответ: сегодня не день рождения.
    core::Result<int> Execute(const core::TenantId& tenant, const core::PersonId& student) const;

private:
    const ports::GuardianshipRepository& guardianships_;
    const ports::BirthDates& birth_dates_;
    const ports::MaturitySettings& maturity_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::identity
