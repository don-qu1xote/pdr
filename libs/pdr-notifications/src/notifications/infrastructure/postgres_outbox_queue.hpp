#pragma once

#include <vector>

#include "core/types/time.hpp"
#include "infrastructure/db/unscoped_access.hpp"
#include "notifications/application/ports/outbox_queue.hpp"

namespace pdr::notifications {

/// Исходящая очередь в Postgres — СО СТОРОНЫ ТОГО, КТО ЕЁ РАЗБИРАЕТ.
///
/// Единственный адаптер в дереве, который строится от `UnscopedAccess` и при
/// этом трогает таблицу с колонкой `tenant_id`. Причина названа в самом перечне
/// (`UnscopedReason::kOutboxDispatch`) и разобрана в
/// docs/adr/0022-outbox-dispatch-across-tenants.md: у отправщика нет практики, а
/// список практик лежит под той же политикой и взять его неоткуда.
///
/// КАЖДЫЙ ЗАПРОС ИДЁТ В СВОЕЙ ТРАНЗАКЦИИ, И ПЕРВЫМ ДЕЛОМ ОБЪЯВЛЯЕТ РАЗБОР.
/// Объявление живёт до конца транзакции: соединение возвращается в пул чистым, и
/// следующий запрос человека видит ровно свою практику. Без объявления таблица
/// не отвечает ничем — это не «доступ ко всему», а вторая дверь со своим ключом.
///
/// Захват и отметка — РАЗНЫЕ транзакции, и это тоже нарочно. Отправка идёт
/// между ними и занимает столько, сколько занимает чужой почтовый узел; держать
/// на ней открытую транзакцию значит держать соединение и блокировку строки
/// ровно столько же.
class PostgresOutboxQueue final : public ports::OutboxQueue {
public:
    explicit PostgresOutboxQueue(const infrastructure::db::UnscopedAccess& unscoped) noexcept;

    std::vector<OutboxEntry> Claim(core::Instant now, core::Instant deadline, int limit) override;

    void Settle(const ports::Settlement& settlement) override;

private:
    const infrastructure::db::UnscopedAccess& unscoped_;
};

}  // namespace pdr::notifications
