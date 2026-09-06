#include "notifications/infrastructure/postgres_outbox_queue.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include <userver/formats/json/value.hpp>
#include <userver/storages/postgres/io/json_types.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"
#include "notifications/core/delivery.hpp"

namespace pdr::notifications {
namespace {

using infrastructure::db::AsInstant;

/// Строка захвата: тот же состав колонок и тот же порядок, что у
/// db/sql/notifications/notifications_outbox_claim.sql.
///
/// Написана руками, а не порождена: у запроса стоит `@no-dto` — разборщик схемы
/// ВЫПОЛНЯЕТ запрос образцовыми значениями, а этот запрос меняет очередь.
/// Разбор при этом всё равно идёт структурой, а не по имени колонки: лишняя,
/// потерянная или переставленная колонка ломает его сразу и целиком.
struct ClaimedRow final {
    std::string tenant_id;
    std::string id;
    std::string event_type;
    userver::formats::json::Value payload;
    std::string dedup_key;
    infrastructure::db::Timestamptz created_at;
    std::int32_t attempts{0};
};

core::TenantId AsTenant(const std::string& text) {
    const auto parsed = core::TenantId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{"notifications_outbox.tenant_id не идентификатор арендатора"};
    }
    return *parsed;
}

OutboxId AsOutbox(const std::string& text) {
    const auto parsed = OutboxId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{"notifications_outbox.id не идентификатор строки очереди"};
    }
    return *parsed;
}

core::PersonId AsPerson(const std::string& text) {
    const auto parsed = core::PersonId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{"notifications_outbox.payload.recipient не человек: " + text};
    }
    return *parsed;
}

Channel AsChannel(const std::string& text) {
    if (text == Name(Channel::kPush)) {
        return Channel::kPush;
    }
    if (text == Name(Channel::kEmail)) {
        return Channel::kEmail;
    }
    throw std::runtime_error{"notifications_outbox.payload.channel вне закрытого списка: " + text};
}

}  // namespace

PostgresOutboxQueue::PostgresOutboxQueue(
    const infrastructure::db::UnscopedAccess& unscoped) noexcept
    : unscoped_{unscoped} {}

std::vector<OutboxEntry> PostgresOutboxQueue::Claim(core::Instant now,
                                                    core::Instant deadline,
                                                    int limit) {
    auto session = unscoped_.Begin(userver::storages::postgres::Transaction::RW);
    session.Execute(sql::kNotificationsDeclareDispatch);

    const auto rows = session.Execute(sql::kNotificationsOutboxClaim, now, deadline, limit);

    std::vector<OutboxEntry> claimed;
    claimed.reserve(rows.Size());
    for (const auto& raw : rows) {
        const auto row = raw.As<ClaimedRow>(userver::storages::postgres::kRowTag);

        /// Письмо собирается из колонок и нагрузки, а не из одной нагрузки:
        /// повод, ключ и момент лежат колонками, потому что по ним ищут и по
        /// ним стоят ограничения. Негодная строка — это исключение, а не отказ:
        /// её положили мы сами, и если она перестала собираться, чинит это
        /// программист, а не повтор.
        const auto delivery =
            Delivery::Compose(AsTenant(row.tenant_id),
                              AsPerson(row.payload["recipient"].As<std::string>()),
                              AsChannel(row.payload["channel"].As<std::string>()),
                              row.event_type,
                              row.dedup_key,
                              AsInstant(row.created_at));
        if (!delivery.HasValue()) {
            throw std::runtime_error{
                "notifications_outbox: строка очереди не собирается в "
                "письмо: " +
                delivery.Failure().Code()};
        }

        claimed.push_back(
            OutboxEntry{AsOutbox(row.id), delivery.Value(), static_cast<int>(row.attempts)});
    }

    session.Commit();
    return claimed;
}

void PostgresOutboxQueue::Settle(const ports::Settlement& settlement) {
    auto session = unscoped_.Begin(userver::storages::postgres::Transaction::RW);
    session.Execute(sql::kNotificationsDeclareDispatch);
    session.Execute(sql::kNotificationsOutboxSettle,
                    settlement.tenant,
                    settlement.id,
                    std::string{Name(settlement.state)},
                    settlement.next_attempt_at,
                    settlement.failed_reason);
    session.Commit();
}

}  // namespace pdr::notifications
