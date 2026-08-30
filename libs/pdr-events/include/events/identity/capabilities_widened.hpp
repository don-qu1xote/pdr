#pragma once

#include <optional>
#include <string_view>

#include "core/types/ids.hpp"
#include "events/envelope.hpp"

namespace pdr::events::identity {

/// Ученик дорос до нового порога: права пришли САМИ.
///
/// Никто их не выдавал и никто не подавал заявки — наступил день рождения.
/// Поэтому событие и нужно: единственный способ узнать о переходе — получить о
/// нём сообщение, и получают его ОБЕ стороны. Ученику — потому что права его;
/// опекуну — потому что часть его прежних обязанностей только что перестала
/// быть его делом, и узнать об этом по упёршейся кнопке хуже всего.
///
/// `guardian` может отсутствовать: у взрослого самостоятельного ученика опекуна
/// нет вовсе, и это не пропуск, а обычное состояние.
struct CapabilitiesWidened final {
    static constexpr std::string_view kType = "identity.capabilities_widened";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::PersonId student;
    std::optional<core::PersonId> guardian;

    /// Код порога (`identity::AgeThreshold`) и сколько лет исполнилось.
    std::string_view threshold;
    int years{0};
};

}  // namespace pdr::events::identity
