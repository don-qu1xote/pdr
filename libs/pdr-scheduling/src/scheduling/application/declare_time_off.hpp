#pragma once

#include <optional>
#include <vector>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "events/bus.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/application/ports/local_days.hpp"
#include "scheduling/application/ports/time_off_repository.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/time_off.hpp"

namespace pdr::scheduling {

/// СЦЕНАРИЙ ПЕРВОГО ДЕЙСТВИЯ: «меня не будет с этого по это».
///
/// ДВА ДЕЙСТВИЯ, А НЕ ОДНО, И НЕ ПЯТЬ. Одно не годится: решение по занятиям
/// принимают, УВИДЕВ занятия, а до этого обращения их никто не показывал. Пять
/// не годится тем более: причина необязательна, а сторона — репетитор это или
/// ученик — не спрашивается вовсе, потому что видна из самих занятий.
///
/// ЗАДНИМ ЧИСЛОМ РАБОТАЕТ, И ЭТО НЕ ПОБЛАЖКА. `now` берётся у часов только для
/// конверта события; ни одной проверки «не в прошлом» здесь нет, и в домене её
/// нет тоже (`TimeOff::Compose` не получает `now` вовсе). Человек заболел в
/// понедельник и дошёл до телефона в среду — это обычный случай, а не обход
/// правила.
///
/// ЗАНЯТИЯ ОТДАЮТСЯ СРАЗУ, А НЕ ВТОРЫМ ЗАПРОСОМ. Иначе между «завёл период» и
/// «увидел, что в нём» помещается ещё одно обращение, которое клиент может не
/// сделать, — и период останется заведённым, а занятия неразобранными.
class DeclareTimeOff final {
public:
    struct Request final {
        core::TenantId tenant;

        /// Кого не будет. Не выводится из актора: перерыв ученику заводит
        /// опекун, а репетитору — он сам или владелец практики.
        core::PersonId person;
        core::PersonId declared_by;

        core::Date from;
        core::Date to;

        /// Пусто — обычный случай. Обязательная причина отсутствия — это
        /// объяснительная, и человек, которому нечего в неё написать, либо не
        /// заведёт период вовсе, либо напишет что попало.
        std::optional<TimeOffReason> reason;

        /// Зона, в которой человек НАЗВАЛ даты, а не та, в которой он их
        /// смотрит.
        core::TimeZone zone;
    };

    struct Answer final {
        TimeOff period;

        /// Занятия, попавшие в период, — те, что ещё занимают время. Отменённые
        /// и прошедшие сюда не попадают: решать по ним нечего.
        std::vector<Lesson> affected;
    };

    DeclareTimeOff(ports::TimeOffRepository& periods,
                   const ports::LessonRepository& lessons,
                   const ports::LocalDays& days,
                   const application::ports::IdGenerator& ids,
                   const application::ports::Clock& clock,
                   events::Bus& bus) noexcept;

    core::Result<Answer> Execute(const Request& request) const;

private:
    ports::TimeOffRepository& periods_;
    const ports::LessonRepository& lessons_;
    const ports::LocalDays& days_;
    const application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

/// ЗАНЯТИЯ ЧЕЛОВЕКА ВНУТРИ ОТРЕЗКА — С ОБЕИХ СТОРОН СРАЗУ.
///
/// Спрашиваются оба названных запроса — «занятия репетитора» и «занятия
/// участника», — и это не расточительность. У репетитора не найдётся занятий,
/// где он ученик, у ученика — где он ведёт; зато сценарий не разветвляется по
/// роли, и «каникулы ученика — то же самое с другой стороны» становится не
/// обещанием в описании, а одним и тем же кодом.
///
/// Отменённые, проведённые и не состоявшиеся отбрасываются: решать по ним
/// нечего, и показывать их человеку как «затронутые» значило бы соврать.
std::vector<Lesson> LessonsInside(const ports::LessonRepository& lessons,
                                  const core::TenantId& tenant,
                                  const core::PersonId& person,
                                  const core::TimeRange& span);

}  // namespace pdr::scheduling
