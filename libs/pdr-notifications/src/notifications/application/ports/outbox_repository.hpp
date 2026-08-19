#pragma once

#include "notifications/core/delivery.hpp"

namespace pdr::notifications::ports {

/// Узкий порт: положить строку в исходящую очередь. Разбирает очередь
/// отправщик — это другой сценарий и другой порт.
class OutboxRepository {
public:
    OutboxRepository(const OutboxRepository&) = delete;
    OutboxRepository& operator=(const OutboxRepository&) = delete;

    virtual ~OutboxRepository() = default;

    virtual void Enqueue(const Delivery& delivery) = 0;

protected:
    OutboxRepository() = default;
};

}  // namespace pdr::notifications::ports
