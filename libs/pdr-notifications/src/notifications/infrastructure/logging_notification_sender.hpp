#pragma once

#include "core/errors.hpp"
#include "notifications/application/ports/notification_sender.hpp"

namespace pdr::notifications {

/// ЗАГЛУШКА ОТПРАВКИ: письмо уходит в журнал и больше никуда.
///
/// Настоящая почта — отдельная задача, и до неё этот адаптер стоит на её месте
/// честно: он ничего не обещает и ничем не притворяется. Очередь при этом
/// работает целиком — строки кладутся, захватываются, повторяются и сдаются, —
/// и когда почта появится, поменяется ровно один компонент в статическом
/// конфиге.
///
/// В ЖУРНАЛ НЕ ПОПАДАЕТ НИ АДРЕСА, НИ ИМЕНИ. Пишется повод, канал и ключ
/// намерения: этого хватает, чтобы найти строку и понять, что с ней было, а
/// личное в журнале не хранится вовсе (docs/legal/personal-data.md).
class LoggingNotificationSender final : public ports::NotificationSender {
public:
    LoggingNotificationSender() = default;

    core::Result<void> Send(const Delivery& delivery) const override;
};

}  // namespace pdr::notifications
