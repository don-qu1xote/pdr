#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/list_lessons.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Сценарий: показать одно занятие ИЗ РАСПИСАНИЯ НАЗВАННОГО ЧЕЛОВЕКА.
///
/// Человек назван не для удобства, а потому, что права спрашиваются про
/// расписание, а не про строку таблицы: «этот ученик мой подопечный» — вопрос,
/// на который можно ответить до всякого чтения, а «чьё это занятие» — нельзя.
/// Спрашивать политику дважды — один раз ни о чём, второй по прочитанному — это
/// две проверки прав, которые разойдутся.
///
/// Поэтому обращение звучит так же, как выборка за отрезок: покажи занятие
/// такое-то из расписания такого-то. Права на расписание уже спрошены, а
/// сценарию остаётся сверить, что занятие в нём и правда есть.
///
/// ЗАНЯТИЯ ЧУЖОГО АРЕНДАТОРА ЗДЕСЬ НЕ БЫВАЕТ, и проверять это нечем: строку
/// отбирает политика базы по объявленному арендатору (ADR-0003), и чужая до
/// сценария не доходит вовсе.
class GetLesson final {
public:
    struct Request final {
        core::TenantId tenant;

        /// Чьё расписание смотрят. Занятие, которого в нём нет, не находится —
        /// и не находится ТАК ЖЕ, как несуществующее: иначе по коду ответа
        /// можно перебрать чужие занятия.
        core::PersonId whose;
        Side side;
        core::LessonId lesson;
    };

    explicit GetLesson(const ports::LessonRepository& lessons) noexcept;

    core::Result<Lesson> Execute(const Request& request) const;

private:
    const ports::LessonRepository& lessons_;
};

}  // namespace pdr::scheduling
