#pragma once

#include <chrono>
#include <string>
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
///
/// РАБОТАЕТ ВНУТРИ ЧУЖОЙ ТРАНЗАКЦИИ, и это главное свойство. Подписка
/// синхронная: издатель публикует событие, обработчик кладёт строку — всё это до
/// коммита, которым закончится обращение. Поэтому «занятие записано, а письма
/// нет» невыразимо: либо коммит, либо ни того ни другого.
///
/// НАПОМИНАНИЯ ЗАВОДЯТСЯ ЗДЕСЬ ЖЕ, СРАЗУ. Занятие записали — строки «за сутки» и
/// «за час» легли в очередь со сроком в будущем и лежат. Крон, который каждые
/// пять минут перебирает все занятия и спрашивает «не пора ли», не нужен вовсе:
/// база умеет ждать лучше, чем цикл по таблице, и стоит это одного индекса.
class DeliverDomainEvents final {
public:
    /// ЗА СКОЛЬКО НАПОМИНАЕМ. Константы, а не динамические величины, и это не
    /// недосмотр: сроки названы в самих типах событий (`ReminderDayBefore`,
    /// `ReminderHourBefore`). Величина, которую можно поменять на три часа,
    /// сделала бы имя типа враньём, а имя типа уезжает в очередь, в журналы и в
    /// чужой код. Понадобится другой срок — это другое напоминание и другой тип.
    static constexpr core::Instant::Duration kDayBefore{std::chrono::hours{24}};
    static constexpr core::Instant::Duration kHourBefore{std::chrono::hours{1}};

    explicit DeliverDomainEvents(ports::OutboxRepository& outbox) noexcept;

    /// Подписаться на всё, о чём стоит сообщить человеку.
    void SubscribeTo(events::Bus& bus);

private:
    /// Оповещение о том, что уже случилось: отправляем, как только доберёмся.
    void Enqueue(const core::TenantId& tenant,
                 const core::PersonId& recipient,
                 Channel channel,
                 std::string_view reason,
                 const std::string& dedup_key,
                 core::Instant at);

    /// Напоминание: та же строка, но со сроком в будущем.
    ///
    /// Момент, который уже прошёл, строки не заводит вовсе. Записанное за
    /// полчаса до начала занятие иначе получило бы «напоминаем: занятие через
    /// час» немедленно, и это выглядело бы поломкой, а не заботой.
    void Remind(const core::TenantId& tenant,
                const core::PersonId& recipient,
                std::string_view reason,
                const core::LessonId& lesson,
                core::Instant starts_at,
                core::Instant::Duration before,
                core::Instant now);

    /// То же самое, но НЕ О ЗАНЯТИИ. Напомнить можно и о выходе из отпуска, а
    /// занятия у такого напоминания нет вовсе: предмет здесь — сам перерыв.
    ///
    /// Отдельным методом, а не вторым `Remind` с другим типом: ключ намерения
    /// собирается одинаково, и собирать его в двух местах значит однажды
    /// собрать по-разному.
    void RemindAbout(const core::TenantId& tenant,
                     const core::PersonId& recipient,
                     std::string_view reason,
                     const std::string& subject,
                     core::Instant due,
                     core::Instant now);

    /// Убрать напоминания об этом занятии: оно больше не состоится в том виде,
    /// в каком о нём собирались напомнить.
    void Forget(const core::TenantId& tenant,
                const core::PersonId& recipient,
                const core::LessonId& lesson);

    ports::OutboxRepository& outbox_;
};

}  // namespace pdr::notifications
