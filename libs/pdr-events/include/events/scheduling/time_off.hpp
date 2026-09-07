#pragma once

#include <cstdint>
#include <string_view>

namespace pdr::events::scheduling {

/// ПОЧЕМУ ЧЕЛОВЕКА НЕ БУДЕТ. НЕОБЯЗАТЕЛЬНА — и это не поблажка, а правило.
///
/// Обязательная причина отсутствия превращает «меня не будет» в объяснительную:
/// человек, которому нечего написать в поле, либо не заведёт период вовсе, либо
/// напишет что попало. Ни то ни другое расписанию не помогает, а первое ещё и
/// оставляет учеников с занятиями, которых не будет.
///
/// ЗАКРЫТЫЙ СПИСОК, А НЕ СТРОКА, и причина тому не только вкус. «Болезнь» в
/// свободном поле — это диагноз в базе: сведения о здоровье, которых мы не
/// собираем и хранить не обязаны (docs/legal/personal-data.md). Категория
/// говорит ровно столько, сколько нужно расписанию, и ни словом больше.
///
/// Список живёт ЗДЕСЬ, в общем реестре событий, а не в контексте расписания:
/// причину читает подписчик — оповещения пишут ученику разными словами про
/// отпуск и про болезнь, — а включать заголовок чужого контекста ему нельзя.
enum class TimeOffReason : std::uint8_t {
    /// Отпуск у репетитора, каникулы у ученика. Одно и то же с разных сторон.
    kVacation,

    /// Болезнь. Категория, а не диагноз: подробностей мы не спрашиваем.
    kSickness,

    /// Перерыв: сессия, переезд, всё остальное, чему не нужно имени.
    kBreak,

    /// ГРАНИЦА СПИСКА, а не причина.
    kBoundary,
};

constexpr std::string_view Name(TimeOffReason reason) noexcept {
    switch (reason) {
        case TimeOffReason::kVacation:
            return "vacation";
        case TimeOffReason::kSickness:
            return "sickness";
        case TimeOffReason::kBreak:
            return "break";
        case TimeOffReason::kBoundary:
            break;
    }
    return "break";
}

/// ОДНО РЕШЕНИЕ НА ВСЕ ЗАНЯТИЯ ПЕРИОДА, А НЕ РЕШЕНИЕ НА КАЖДОЕ.
///
/// Двенадцать занятий за две недели отпуска — это двенадцать одинаковых ответов
/// на один и тот же вопрос, и человек, которому их задали поштучно, бросает на
/// пятом. Поштучный разбор в продукте есть — это обычная отмена и обычный
/// перенос, — но ЕДИНСТВЕННЫМ путём он быть не может.
enum class TimeOffDecision : std::uint8_t {
    /// Отменить все. Без удержания, если это репетитор: удерживать с ученика за
    /// то, что занятие отменил не он, нельзя никогда (PDR-SCHED-05).
    kCancel,

    /// Перенести все на после периода, сохранив день недели и час.
    kPostpone,

    /// Оставить как есть. Тоже решение: репетитор уезжает, но занятия ведёт.
    kKeep,

    /// ГРАНИЦА СПИСКА, а не решение.
    kBoundary,
};

constexpr std::string_view Name(TimeOffDecision decision) noexcept {
    switch (decision) {
        case TimeOffDecision::kCancel:
            return "cancel";
        case TimeOffDecision::kPostpone:
            return "postpone";
        case TimeOffDecision::kKeep:
            return "keep";
        case TimeOffDecision::kBoundary:
            break;
    }
    return "keep";
}

}  // namespace pdr::events::scheduling
