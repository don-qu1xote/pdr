#pragma once

#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/lesson_history.hpp"

namespace pdr::scheduling::ports {

/// История занятия: записать строку и прочитать всё занятие целиком.
///
/// ЗАПИСЬ НЕ УДАЛЯЕТСЯ И НЕ ПРАВИТСЯ, и метода на это здесь нет. Историю читают
/// в споре «я отменял заранее»; история, которую можно поправить, спор не
/// решает, а переводит в спор о самой истории.
///
/// Читается ЦЕЛИКОМ, а не за отрезок: у одного занятия строк единицы, и выборка
/// «последние N» потребовала бы индекса под запрос, которого никто не задаёт.
class LessonHistory {
public:
    LessonHistory(const LessonHistory&) = delete;
    LessonHistory& operator=(const LessonHistory&) = delete;

    virtual ~LessonHistory() = default;

    virtual core::Result<void> Record(const LessonHistoryEntry& entry) = 0;

    /// Строки занятия по возрастанию момента.
    virtual std::vector<LessonHistoryEntry> Of(const core::TenantId& tenant,
                                               const core::LessonId& lesson) const = 0;

protected:
    LessonHistory() = default;
};

}  // namespace pdr::scheduling::ports
