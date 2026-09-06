#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "core/types/ids.hpp"
#include "notifications/core/delivery.hpp"

namespace pdr::notifications {

/// Идентификатор строки очереди. Свой, а не платформенный: очередь — внутреннее
/// устройство оповещений, и чужим контекстам она неизвестна.
using OutboxId = core::StrongId<struct OutboxTag>;

/// СОСТОЯНИЕ СТРОКИ ОЧЕРЕДИ. Три и только три.
///
/// «Отправляется» среди них нет намеренно. Захват строки виден не состоянием, а
/// сроком: он сдвинут вперёд, и пока не истёк, строку никто не берёт. Четвёртое
/// состояние пришлось бы снимать тому воркеру, который его поставил, — а он мог
/// умереть, и снимать было бы некому. Ровно та же ошибка, что «update ... set
/// locked = true» у самодельных блокировок (ADR-0011).
enum class OutboxState : std::uint8_t {
    /// Ждёт своего часа. Час может быть и завтрашним: напоминание — это
    /// `pending` со сроком в будущем.
    kPending,

    /// Ушло. Что дальше сделал с письмом почтовый узел, очередь не знает и знать
    /// не может: это журнал доставки, а не она.
    kSent,

    /// Попытки исчерпаны. Строка остаётся лежать с причиной: удалить её значит
    /// потерять единственный след того, что человеку не написали.
    kGaveUp,

    /// ГРАНИЦА СПИСКА, а не состояние.
    kBoundary,
};

std::string_view Name(OutboxState state) noexcept;

/// Все состояния подряд. Единственный способ обойти список целиком.
inline constexpr std::array<OutboxState, 3> kEveryOutboxState{
    OutboxState::kPending,
    OutboxState::kSent,
    OutboxState::kGaveUp,
};

static_assert(kEveryOutboxState.size() == static_cast<std::size_t>(OutboxState::kBoundary),
              "состояние заведено, а в kEveryOutboxState его нет: обход пропустит его, и "
              "«у каждого состояния есть значение в схеме» станет непроверяемым");

/// СТРОКА ОЧЕРЕДИ, ВЗЯТАЯ В РАБОТУ.
///
/// Это не `Delivery` с довеском: `Delivery` — само письмо (кому, куда, по какому
/// поводу), а здесь к нему добавлено то, что знает очередь и не знает письмо, —
/// который это заход. Число попыток нужно ровно затем, чтобы спросить политику
/// «пробовать ли ещё», и больше ни для чего.
struct OutboxEntry final {
    OutboxId id;
    Delivery delivery;

    /// Сколько попыток уже сделано, включая текущую: захват считает её сразу,
    /// не дожидаясь исхода. Воркер, упавший молча, иначе не тратил бы попыток
    /// вовсе.
    int attempts{0};
};

}  // namespace pdr::notifications
