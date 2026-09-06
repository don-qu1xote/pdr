#pragma once

#include "core/errors.hpp"
#include "notifications/core/delivery.hpp"

namespace pdr::notifications::ports {

/// КУДА ПИСЬМО УХОДИТ НА САМОМ ДЕЛЕ.
///
/// Порт один на все каналы, и канал в нём — поле письма, а не второй метод:
/// сценарий разбора очереди не выбирает между почтой и push, он отдаёт письмо и
/// узнаёт, ушло ли. Выбор делает адаптер, потому что это его знание.
///
/// Отказ возвращается значением, а не исключением. «Почтовый узел не ответил» —
/// обычный исход, из-за которого строка ложится на повтор; исключение здесь
/// означало бы, что каждый неответ сети надо ловить и превращать в значение
/// вручную, и однажды кто-нибудь не поймает.
///
/// Адаптеров сегодня один — запись в журнал. Настоящая почта заводится отдельной
/// задачей, и когда заведётся, ни сценарий, ни очередь, ни таблица не поменяются
/// ни строкой: поменяется одно поле в сборке отправщика. В этом и смысл порта.
class NotificationSender {
public:
    NotificationSender(const NotificationSender&) = delete;
    NotificationSender& operator=(const NotificationSender&) = delete;

    virtual ~NotificationSender() = default;

    virtual core::Result<void> Send(const Delivery& delivery) const = 0;

protected:
    NotificationSender() = default;
};

}  // namespace pdr::notifications::ports
