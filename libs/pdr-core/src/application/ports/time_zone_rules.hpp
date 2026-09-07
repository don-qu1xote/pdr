#pragma once

#include "core/errors.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"

namespace pdr::application::ports {

/// ПРАВИЛА ЗОНЫ ЗНАЧЕНИЕМ: начальное смещение и переводы часов на отрезке.
///
/// Порт, названный в `core/types/local_time.hpp` с первого дня календаря и до
/// сих пор не заведённый: он нужен тому, кто отдаёт расписание НАРУЖУ. Пока
/// расписание жило только у нас, хватало моментов; лента подписки обязана
/// назвать зону и её переводы по стандарту (VTIMEZONE), и придумать их нельзя.
///
/// ОТРЕЗОК ОБЯЗАТЕЛЕН, и это не оптимизация. Переводов часов у зоны за всю её
/// историю сотни: Москва меняла правила пять раз за пятнадцать лет. В ленту
/// уезжают те, что попадают в её горизонт, — остальные только раздувают ответ и
/// заставляют календарь разбирать столетие ради двух недель.
///
/// НАЧАЛЬНОЕ СМЕЩЕНИЕ — то, что действует В НАЧАЛЕ отрезка, а не в начале
/// времён: `core::ZoneOffsets::At` отвечает на вопрос «сколько сейчас», и вне
/// запрошенного отрезка отвечать ему нечем.
///
/// Отказ — обычное значение: имя зоны проверяется формой (`core::TimeZone`), а
/// не существованием, и «Europe/Mordor» доходит сюда живым.
class TimeZoneRules {
public:
    TimeZoneRules(const TimeZoneRules&) = delete;
    TimeZoneRules& operator=(const TimeZoneRules&) = delete;

    virtual ~TimeZoneRules() = default;

    virtual core::Result<core::ZoneOffsets> For(const core::TimeZone& zone,
                                                const core::TimeRange& covering) const = 0;

protected:
    TimeZoneRules() = default;
};

}  // namespace pdr::application::ports
