#pragma once

#include <string>
#include <vector>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "notifications/core/outbox_entry.hpp"

namespace pdr::notifications::ports {

/// Чем кончилась попытка: что записать в строку очереди.
///
/// Три исхода одним типом, а не тремя методами: они отличаются значениями, а не
/// смыслом — «больше не должна», «пробуем позже», «больше не пробуем». Три
/// метода разошлись бы на первой же правке, и фейку пришлось бы держать три
/// одинаковых реализации.
struct Settlement final {
    core::TenantId tenant;
    OutboxId id;
    OutboxState state{OutboxState::kPending};

    /// Когда пробовать снова. У ушедшей и у сдавшейся строки не значит ничего и
    /// остаётся тем, что было: колонка `not null`, а «никогда» в ней не
    /// выражается.
    core::Instant next_attempt_at;

    /// Почему сдались. Пусто у всех исходов, кроме `gave_up`, — и это не
    /// договорённость, а ограничение схемы.
    std::string failed_reason;
};

/// РАЗБОР ОЧЕРЕДИ: взять то, чему настал срок, и записать, чем кончилось.
///
/// Порт отдельный от `OutboxRepository`, и это не педантизм. Класть письмо
/// умеет каждый сценарий, у которого есть сессия обращения; разбирать очередь
/// умеет один фоновый отправщик, и ходит он не так — сразу у всех практик,
/// потому что своей практики у него нет. Один порт с четырьмя методами заставил
/// бы каждый фейк отвечать на вопросы, которых ему никто не задаёт.
class OutboxQueue {
public:
    OutboxQueue(const OutboxQueue&) = delete;
    OutboxQueue& operator=(const OutboxQueue&) = delete;

    virtual ~OutboxQueue() = default;

    /// Забрать себе не больше `limit` строк, чей срок настал.
    ///
    /// ЗАБРАТЬ, А НЕ ПОСМОТРЕТЬ. Возвращённая строка уже посчитана попыткой и
    /// уже сдвинута по сроку на `deadline`: вторая реплика, проснувшаяся в ту же
    /// секунду, получит другие строки. Упавший на отправке воркер ничего не
    /// держит — `deadline` наступит сам, и строка вернётся в работу.
    ///
    /// Докуда держать, решает не адаптер: это `RetryPolicy::Deadline`, то есть
    /// величина из настроек, а не из соединения.
    virtual std::vector<OutboxEntry> Claim(core::Instant now,
                                           core::Instant deadline,
                                           int limit) = 0;

    virtual void Settle(const Settlement& settlement) = 0;

protected:
    OutboxQueue() = default;
};

}  // namespace pdr::notifications::ports
