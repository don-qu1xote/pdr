#include "notifications/infrastructure/postgres_outbox.hpp"

#include <string>

#include <pdr/sql_queries.hpp>

#include <userver/formats/json/value_builder.hpp>

#include "infrastructure/db/domain_types.hpp"
#include "notifications/core/outbox_entry.hpp"

namespace pdr::notifications {
namespace {

/// НАГРУЗКА ПИСЬМА — то, что нужно отправщику и получателю, и ничего сверх.
///
/// Ключ намерения лежит здесь ВТОРОЙ РАЗ, рядом с колонкой. Колонка нужна базе —
/// по ней стоит уникальность; нагрузка уезжает получателю, и повтор он узнаёт
/// именно из неё. Читать колонки чужой таблицы получателю нечем.
userver::formats::json::Value Payload(const Delivery& delivery) {
    userver::formats::json::ValueBuilder payload{userver::formats::json::Type::kObject};
    payload["recipient"] = delivery.Recipient().ToString();
    payload["channel"] = std::string{Name(delivery.DeliveryChannel())};
    payload["dedup_key"] = delivery.DedupKey();
    return payload.ExtractValue();
}

}  // namespace

PostgresOutbox::PostgresOutbox(infrastructure::db::ScopedTenantContext& scope,
                               const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

void PostgresOutbox::Enqueue(const Delivery& delivery, core::Instant due_at) {
    scope_.Session().Execute(sql::kNotificationsOutboxEnqueue,
                             delivery.Tenant(),
                             ids_.Next<OutboxId>(),
                             delivery.Reason(),
                             Payload(delivery),
                             delivery.DedupKey(),
                             delivery.CreatedAt(),
                             due_at);
}

void PostgresOutbox::Withdraw(const core::TenantId& tenant, const std::string& dedup_key) {
    scope_.Session().Execute(sql::kNotificationsOutboxWithdraw, tenant, dedup_key);
}

}  // namespace pdr::notifications
