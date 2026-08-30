#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "identity/core/capabilities.hpp"

namespace pdr::identity {

/// Поступок, который подросток совершает САМ — и о котором опекун узнаёт всегда.
///
/// СМЫСЛ ПОРОГА — САМОСТОЯТЕЛЬНОСТЬ, А НЕ ТАЙНА. Подросток распоряжается своим
/// расписанием без разрешения, а родитель знает об этом фактом, а не
/// расследованием. Уведомление не настраивается, не выключается и не зависит от
/// уровней доступа опекуна: выключателя нет нигде в дереве, и это проверяет
/// `scripts/check_guardian_notice.py`.
///
/// ЧТО ИМЕННО НАПИСАНО В ОТЗЫВЕ, ОПЕКУН НЕ ВИДИТ. Право высказаться без надзора
/// и есть содержание порога: «отзыв написан» — это факт, а текст — разговор
/// ученика с чужими людьми. Поэтому поступок и есть код из этого списка, а не
/// строка: места для текста в нём нет.
enum class IndependentAct : std::uint8_t {
    kLessonRescheduled,
    kLessonCancelled,
    kReviewWritten,

    /// ГРАНИЦА СПИСКА, а не поступок.
    kBoundary,
};

std::string_view Name(IndependentAct act) noexcept;

std::optional<IndependentAct> ParseIndependentAct(std::string_view text);

inline constexpr std::array<IndependentAct, 3> kEveryIndependentAct{
    IndependentAct::kLessonRescheduled,
    IndependentAct::kLessonCancelled,
    IndependentAct::kReviewWritten,
};

static_assert(kEveryIndependentAct.size() == static_cast<std::size_t>(IndependentAct::kBoundary),
              "поступок заведён, а в kEveryIndependentAct его нет: опекуну о нём не сообщат");

/// Какая возможность делает этот поступок самостоятельным.
///
/// Отсюда же и порог, с которого о нём сообщают: пока возможности нет, поступок
/// совершает опекун, и уведомлять его о самом себе незачем.
constexpr Capability Enables(IndependentAct act) noexcept {
    switch (act) {
        case IndependentAct::kLessonRescheduled:
        case IndependentAct::kLessonCancelled:
            return Capability::kMoveOwnSlots;
        case IndependentAct::kReviewWritten:
        case IndependentAct::kBoundary:
            return Capability::kWriteReview;
    }
    return Capability::kWriteReview;
}

}  // namespace pdr::identity
