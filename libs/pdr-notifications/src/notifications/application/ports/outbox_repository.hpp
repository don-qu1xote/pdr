#pragma once

#include <string>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "notifications/core/delivery.hpp"

namespace pdr::notifications::ports {

/// Узкий порт: положить строку в исходящую очередь и забрать её назад, пока она
/// не ушла. Разбирает очередь отправщик — это другой сценарий и другой порт.
///
/// ПИШЕТ В ТУ ЖЕ ТРАНЗАКЦИЮ, ЧТО И ИЗДАТЕЛЬ. Адаптер этого порта строится от
/// сессии обращения, а не от пула: строка очереди и изменение, о котором она
/// рассказывает, коммитятся вместе или не коммитятся вовсе. Отдельная
/// транзакция под очередь обнуляет весь смысл затеи — между двумя коммитами
/// помещается падение.
class OutboxRepository {
public:
    OutboxRepository(const OutboxRepository&) = delete;
    OutboxRepository& operator=(const OutboxRepository&) = delete;

    virtual ~OutboxRepository() = default;

    /// Положить письмо со сроком отправки.
    ///
    /// Срок в прошлом или в настоящем означает «как только доберёмся», в будущем
    /// — отложенное письмо: напоминание о завтрашнем занятии лежит в очереди со
    /// вчера и ждёт своего часа. Никакого крона по занятиям для этого не нужно.
    ///
    /// ПОВТОРНЫЙ ВЫЗОВ С ТЕМ ЖЕ КЛЮЧОМ НАМЕРЕНИЯ НЕ КЛАДЁТ ВТОРОГО: он двигает
    /// срок и нагрузку у той же строки, пока она не ушла. Отсюда и переносы:
    /// занятие переехало — напоминание переехало вместе с ним, а не осталось на
    /// прежнем часе.
    virtual void Enqueue(const Delivery& delivery, core::Instant due_at) = 0;

    /// Забрать неотправленное. Занятие отменили — напоминание «завтра в 17:00»
    /// не должно уйти; ушедшее при этом не трогается, потому что это уже факт.
    virtual void Withdraw(const core::TenantId& tenant, const std::string& dedup_key) = 0;

protected:
    OutboxRepository() = default;
};

}  // namespace pdr::notifications::ports
