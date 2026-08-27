#pragma once

#include <cstdint>
#include <string_view>

#include "core/types/ids.hpp"
#include "events/envelope.hpp"

namespace pdr::events::identity {

/// Что именно подопечный сделал сам. ЗАКРЫТЫЙ СПИСОК КОДОВ, А НЕ СТРОКА.
///
/// Строка в этом месте — это поле, в которое однажды положат текст отзыва:
/// не по злому умыслу, а потому, что «удобно передать заодно». Перечисление
/// такого не позволяет вовсе, и `scripts/check_guardian_notice.py` следит,
/// чтобы в структуре события не завелось поля со строковым типом.
enum class WardAct : std::uint8_t {
    kLessonRescheduled,
    kLessonCancelled,
    kReviewWritten,
};

/// Код поступка: то же слово, что в поводе строки очереди оповещений.
constexpr std::string_view Name(WardAct act) noexcept {
    switch (act) {
        case WardAct::kLessonRescheduled:
            return "identity.ward_rescheduled_lesson";
        case WardAct::kLessonCancelled:
            return "identity.ward_cancelled_lesson";
        case WardAct::kReviewWritten:
            return "identity.ward_wrote_review";
    }
    return "identity.ward_acted_alone";
}

/// Подопечный сделал что-то САМ, и опекун об этом узнаёт. Всегда.
///
/// СМЫСЛ ВОЗРАСТНОГО ПОРОГА — САМОСТОЯТЕЛЬНОСТЬ, А НЕ ТАЙНА. Подросток
/// распоряжается своим расписанием без разрешения, а родитель знает об этом
/// фактом, а не расследованием. Выключателя у этого события нет: ни настройки,
/// ни уровня доступа, ни согласия — `scripts/check_guardian_notice.py` роняет
/// сборку, если он заведётся.
///
/// ТЕКСТА ЗДЕСЬ НЕТ И БЫТЬ НЕ МОЖЕТ. Опекун видит, что отзыв написан, и не
/// видит, что в нём: право высказаться без надзора и есть содержание порога.
/// Поэтому поступок — код, а не строка, и положить в него нечего.
struct WardActedAlone final {
    static constexpr std::string_view kType = "identity.ward_acted_alone";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::PersonId guardian;
    core::PersonId student;
    WardAct act{WardAct::kLessonRescheduled};
};

}  // namespace pdr::events::identity
