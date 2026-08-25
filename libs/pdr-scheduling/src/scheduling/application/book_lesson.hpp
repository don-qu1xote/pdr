#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/bus.hpp"
#include "identity/contract.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Сценарий: записать ученика на занятие.
///
/// Право записываться за ученика спрашивается у identity — по его публичному
/// контракту, единственному заголовку, который этот модуль о нём знает. Ни
/// одной таблицы identity здесь нет и никакого «сходим посмотрим в
/// identity_guardianship» тоже: чужая таблица не читается ни при каких
/// обстоятельствах.
class BookLesson final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId actor;
        core::PersonId tutor;
        core::PersonId student;
        core::Instant starts_at;
        Lesson::Duration duration;
    };

    BookLesson(ports::LessonRepository& lessons,
               const identity::Contract& identity,
               const application::ports::Clock& clock,
               const application::ports::IdGenerator& ids,
               events::Bus& bus) noexcept;

    core::Result<core::LessonId> Execute(const Request& request) const;

private:
    ports::LessonRepository& lessons_;
    const identity::Contract& identity_;
    const application::ports::Clock& clock_;
    const application::ports::IdGenerator& ids_;
    events::Bus& bus_;
};

}  // namespace pdr::scheduling
