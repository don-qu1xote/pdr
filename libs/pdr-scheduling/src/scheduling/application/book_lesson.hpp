#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/bus.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Сценарий: записать ученика на занятие.
///
/// ПРАВА ЗДЕСЬ НЕ СПРАШИВАЮТСЯ, и это не упущение. Их спрашивает форма запроса
/// — один раз, до всякой работы, и спрашивает у политики
/// (`Action::kBookLesson` над ресурсом «репетитор и ученик»). Вторая проверка
/// на этом же пути не усиливает первую, а расходится с ней: политика разрешает
/// репетитору его собственный слот, а «вправе ли он действовать ЗА ученика» —
/// вопрос про другое, и на нём тот же репетитор получал бы отказ.
///
/// Кто вправе записывать за кого — по-прежнему решение identity, а не этого
/// модуля: оно живёт в `SchedulingPolicy` и проверяется матрицей прав.
class BookLesson final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId tutor;
        core::PersonId student;
        core::Instant starts_at;
        Lesson::Duration duration;

        /// Зона, в которой человек назвал время. Не зона показа: занятие,
        /// назначенное на 18:00 по Берлину, обязано остаться в 18:00 по
        /// Берлину и после перевода часов.
        core::TimeZone zone;
    };

    BookLesson(ports::LessonRepository& lessons,
               const application::ports::Clock& clock,
               const application::ports::IdGenerator& ids,
               events::Bus& bus) noexcept;

    /// ЗАНЯТИЕ ЦЕЛИКОМ, а не его идентификатор: у записанного занятия есть
    /// конец, зона и состояние, и вызывающему они нужны все. Один
    /// идентификатор заставил бы его сходить за тем же самым второй раз — в
    /// той же транзакции, за только что записанной строкой.
    core::Result<Lesson> Execute(const Request& request) const;

private:
    ports::LessonRepository& lessons_;
    const application::ports::Clock& clock_;
    const application::ports::IdGenerator& ids_;
    events::Bus& bus_;
};

}  // namespace pdr::scheduling
