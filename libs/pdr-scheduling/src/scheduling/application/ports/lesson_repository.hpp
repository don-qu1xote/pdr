#pragma once

#include <optional>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling::ports {

/// Занятия: названные запросы и сохранение. Больше ничего.
///
/// КАЖДЫЙ ЗАПРОС НАЗВАН. Метода `FindByCriteria(const Criteria&)` здесь нет и
/// не будет: он возвращает абстракцию, которая протекает, — под «критерием»
/// рано или поздно оказывается SQL, собранный на слое сценариев, и индексы под
/// него завести нечем, потому что запрос заранее неизвестен. Названный запрос
/// знает свой индекс: `scheduling_lesson_by_tutor` и
/// `scheduling_lesson_by_participant` заведены под две выборки за диапазон и
/// ни подо что ещё, а адресные — `Find` и `FindAtSlot` — идут по ключу и по
/// тому же индексу репетитора, и своего не просят.
class LessonRepository {
public:
    LessonRepository(const LessonRepository&) = delete;
    LessonRepository& operator=(const LessonRepository&) = delete;

    virtual ~LessonRepository() = default;

    /// Одно занятие по его идентификатору. Своего индекса не просит: занятие
    /// адресуется первичным ключом, и он уже есть.
    virtual std::optional<Lesson> Find(const core::TenantId& tenant,
                                       const core::LessonId& id) const = 0;

    /// Занято ли у репетитора это время. Отдельно от `OfTutor`, потому что
    /// отвечает на другой вопрос и обходится одной строкой вместо диапазона.
    virtual std::optional<Lesson> FindAtSlot(const core::TenantId& tenant,
                                             const core::PersonId& tutor,
                                             core::Instant starts_at) const = 0;

    /// Занятия репетитора за диапазон, по возрастанию начала.
    virtual std::vector<Lesson> OfTutor(const core::TenantId& tenant,
                                        const core::PersonId& tutor,
                                        const core::TimeRange& window) const = 0;

    /// Занятия участника за диапазон, по возрастанию начала.
    virtual std::vector<Lesson> OfParticipant(const core::TenantId& tenant,
                                              const core::PersonId& participant,
                                              const core::TimeRange& window) const = 0;

    /// СОХРАНЕНИЕ МОЖЕТ ОТКАЗАТЬ, И ЭТО ЧАСТЬ ПОРТА.
    ///
    /// Доменная проверка пересечений смотрит на то, что прочитала, а между
    /// чтением и записью помещается второе бронирование. Гонку ловит база
    /// (`scheduling_lesson_no_overlap`), и её отказ обязан дойти до сценария
    /// значением, а не исключением: «слот занят» — обычный ответ домена.
    ///
    /// Фейк обязан отказывать в том же случае: иначе unit-прогон зелен на
    /// поведении, которого в проде нет.
    virtual core::Result<void> Save(const Lesson& lesson) = 0;

protected:
    LessonRepository() = default;
};

}  // namespace pdr::scheduling::ports
