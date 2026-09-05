#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::scheduling {

/// Идентификатор строки истории. Свой, а не платформенный: история — запись
/// расписания, и чужим контекстам она неизвестна.
using LessonHistoryId = core::StrongId<struct LessonHistoryTag>;

/// Что случилось с занятием. ЗАКРЫТЫЙ СПИСОК, а не свободная строка: историю
/// читают в споре, и «отменено», «Отменено» и «отмена» окажутся в ней тремя
/// разными действиями ровно в тот день, когда спор случится.
enum class LessonAction : std::uint8_t {
    kBooked,
    kConfirmed,
    kRescheduled,
    kCancelledByStudent,
    kCancelledByTutor,
    kHeld,
    kNoShow,

    /// ГРАНИЦА СПИСКА, а не действие.
    kBoundary,
};

std::string_view Name(LessonAction action) noexcept;

/// Все действия подряд. Единственный способ обойти список целиком.
inline constexpr std::array<LessonAction, 7> kEveryLessonAction{
    LessonAction::kBooked,
    LessonAction::kConfirmed,
    LessonAction::kRescheduled,
    LessonAction::kCancelledByStudent,
    LessonAction::kCancelledByTutor,
    LessonAction::kHeld,
    LessonAction::kNoShow,
};

static_assert(kEveryLessonAction.size() == static_cast<std::size_t>(LessonAction::kBoundary),
              "действие заведено, а в kEveryLessonAction его нет: обход пропустит его, и "
              "«каждое действие попадает в историю» станет непроверяемым");

/// ЗАПИСЬ ИСТОРИИ ЗАНЯТИЯ — НЕ АУДИТ РАДИ АУДИТА.
///
/// Спор «я отменял заранее» возникает гарантированно, и разрешает его не память
/// участников, а строка с моментом. Поэтому запись несёт КТО, ЧТО, КОГДА — и
/// подробности, которые в закрытый список не влезают: во сколько занятие стояло
/// до переноса, сколько удержано и почему.
///
/// Момент — по часам издателя, то есть по порту `Clock`: история, у которой
/// время взято у системы, в тесте не проверяется, а в споре не воспроизводится.
struct LessonHistoryEntry final {
    core::TenantId tenant;
    core::LessonId lesson;

    /// Кто это сделал. Человек, а не роль: роль со временем меняется, а вопрос
    /// «кто нажал» ответа со временем не меняет.
    core::PersonId actor;

    LessonAction action{LessonAction::kBooked};
    core::Instant at;

    /// Подробности словами, в свободной форме. РЕШЕНИЙ ПО НИМ НЕ ПРИНИМАЮТ:
    /// всё, на чём стоят правила, лежит в полях выше и в самом занятии. Здесь —
    /// то, что человек прочитает в споре и что закрытым списком не выражается.
    std::string details;

    friend bool operator==(const LessonHistoryEntry&, const LessonHistoryEntry&) = default;
};

}  // namespace pdr::scheduling
