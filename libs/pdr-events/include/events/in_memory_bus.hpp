#pragma once

#include <typeindex>
#include <unordered_map>
#include <vector>

#include "events/bus.hpp"

namespace pdr::events {

/// Доставка внутри процесса, синхронно: в первой фазе бинарник один, и
/// пересылать событие через сеть некуда.
///
/// Транзакционный outbox (событие пишется в той же транзакции, что и изменение)
/// и доставка между процессами появятся вместе с первой миграцией — это область
/// DB и INF. Интерфейс Bus при этом не поменяется, поменяется реализация: ровно
/// поэтому подписчик зависит от Bus, а не от способа доставки.
class InMemoryBus final : public Bus {
public:
    InMemoryBus() = default;

    /// Сколько событий прошло через шину — удобно в тестах.
    std::size_t Published() const noexcept {
        return published_;
    }

protected:
    void PublishErased(std::type_index type, const void* event) override;
    void SubscribeErased(std::type_index type, ErasedHandler handler) override;

private:
    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
    std::size_t published_{0};
};

}  // namespace pdr::events
