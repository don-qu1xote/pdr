#pragma once

#include <cstdint>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "scheduling/application/ports/lesson_repository.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {

/// Кем человек приходится занятиям, которые просит показать.
///
/// Не роль в кабинете, а СТОРОНА ЗАНЯТИЯ: один и тот же человек ведёт свои
/// занятия и учится на чужих, и это два разных расписания. Порт называет их
/// двумя запросами (`OfTutor` и `OfParticipant`), и здесь ровно тот же выбор:
/// третьего способа посмотреть занятия нет, потому что третьего запроса нет.
enum class Side : std::uint8_t {
    kTutor,
    kParticipant,
};

/// Сценарий: показать занятия за отрезок.
///
/// ОДИН СЦЕНАРИЙ НА ТРОИХ — репетитора, ученика и опекуна. Разные у них не
/// действия, а права: кто чьё расписание вправе увидеть, решает политика
/// (`Action::kViewSchedule`) до того, как сюда дойдёт дело, и решает по тому,
/// кем спрашивающий приходится названному человеку. Сценарию после этого
/// остаётся выбор между двумя названными запросами порта — и больше ничего.
///
/// Ни одного «а если он ученик, то…» здесь поэтому нет: такое условие означало
/// бы вторую проверку прав, которая разойдётся с первой.
class ListLessons final {
public:
    struct Request final {
        core::TenantId tenant;

        /// Чьи занятия показать. Не обязательно сам спрашивающий: опекун
        /// смотрит расписание подопечного, владелец практики — репетитора.
        core::PersonId whose;
        Side side;
        core::TimeRange window;
    };

    explicit ListLessons(const ports::LessonRepository& lessons) noexcept;

    core::Result<std::vector<Lesson>> Execute(const Request& request) const;

private:
    const ports::LessonRepository& lessons_;
};

}  // namespace pdr::scheduling
