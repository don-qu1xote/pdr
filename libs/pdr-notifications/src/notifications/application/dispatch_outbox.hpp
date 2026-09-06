#pragma once

#include <cstddef>

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "notifications/application/ports/notification_sender.hpp"
#include "notifications/application/ports/outbox_queue.hpp"
#include "notifications/application/ports/retry_policies.hpp"

namespace pdr::notifications {

/// Чем кончился один проход отправщика. Числа для метрики и для теста.
struct DispatchReport final {
    std::size_t claimed{0};
    std::size_t sent{0};
    std::size_t retried{0};
    std::size_t gave_up{0};
};

/// РАЗОБРАТЬ ОЧЕРЕДЬ: взять то, чему настал срок, отправить и записать исход.
///
/// Сценарий, а не компонент: ни таймера, ни соединения, ни логгера здесь нет —
/// только три порта. Поэтому «попытки исчерпались, и строка сдалась»
/// проверяется без базы и без часов операционной системы, а не поднятым
/// сервисом и секундомером.
///
/// ПОРЯДОК ЗДЕСЬ — ЭТО И ЕСТЬ ГАРАНТИЯ. Строка сначала захватывается (и
/// коммитится захваченной), и только потом отправляется. Обратный порядок —
/// «отправить, потом отметить» — теряет письмо ровно в том случае, ради
/// которого всё написано: процесс умер между отправкой и отметкой, и письмо
/// уйдёт второй раз. Оно и уйдёт: доставка обещана «не менее одного раза», а
/// повтор узнаётся получателем по ключу намерения. Обещать «ровно один раз»
/// без участия получателя нельзя вовсе, и обещать это мы не будем.
///
/// ОДИН ПРОХОД — НЕ ВСЯ ОЧЕРЕДЬ. Берётся не больше `limit` строк: проход,
/// который разбирает очередь до дна, держит соединение столько, сколько в ней
/// накопилось, и первым же всплеском выедает пул. Не успели — доберём
/// следующим проходом, строки никуда не денутся.
class DispatchOutbox final {
public:
    DispatchOutbox(ports::OutboxQueue& queue,
                   const ports::NotificationSender& sender,
                   const ports::RetryPolicies& policies,
                   const application::ports::Clock& clock) noexcept;

    core::Result<DispatchReport> Execute(int limit) const;

private:
    ports::OutboxQueue& queue_;
    const ports::NotificationSender& sender_;
    const ports::RetryPolicies& policies_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::notifications
