#pragma once

#include <string_view>

#include "events/envelope.hpp"

namespace pdr::events::identity {

/// Практика поменяла своё состояние видимости снаружи.
///
/// Одно событие на все переходы, а не четыре: слушателю нужен ответ на один
/// вопрос — «попадает ли эта практика в подбор», — и `discoverable` отвечает на
/// него прямо. Состояние едет рядом кодом, для тех, кому нужна причина.
///
/// Слушатели у него разные и оба будущие: `matching` перестраивает выдачу,
/// `notifications` пишет хозяину практики, чем кончился разбор.
struct PracticeVisibilityChanged final {
    static constexpr std::string_view kType = "identity.practice_visibility_changed";
    static constexpr int kVersion = 1;

    Envelope envelope;

    /// Код состояния (`identity::Visibility`).
    std::string_view visibility;

    /// Попадает ли практика в подбор прямо сейчас. По умолчанию — нет, у всех.
    bool discoverable{false};
};

}  // namespace pdr::events::identity
