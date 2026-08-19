#pragma once

#include "events/bus.hpp"
#include "notifications/application/ports/outbox_repository.hpp"

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
    ports::OutboxRepository& outbox_;
};

}  // namespace pdr::notifications
