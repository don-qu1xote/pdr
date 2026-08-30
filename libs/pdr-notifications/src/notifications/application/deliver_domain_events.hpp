#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "events/bus.hpp"
#include "notifications/application/ports/outbox_repository.hpp"
#include "notifications/core/delivery.hpp"

namespace pdr::notifications {

/// Сценарий: превращать доменные события в строки исходящей очереди.
///
/// Здесь и видно, ради чего заведён общий реестр событий. Этот файл знает про
/// identity и scheduling ровно то, что написано в events/: тип события и его
/// поля. Ни одного их заголовка, ни одной их таблицы, ни одной строчки в их
/// коде — чтобы добавить оповещение, издателя не открывают.
class DeliverDomainEvents final {
public:
    explicit DeliverDomainEvents(ports::OutboxRepository& outbox) noexcept;

    /// Подписаться на всё, о чём стоит сообщить человеку.
    void SubscribeTo(events::Bus& bus);

private:
    void Enqueue(const core::TenantId& tenant,
                 const core::PersonId& recipient,
                 Channel channel,
                 std::string_view reason,
                 core::Instant at);

    ports::OutboxRepository& outbox_;
};

}  // namespace pdr::notifications
