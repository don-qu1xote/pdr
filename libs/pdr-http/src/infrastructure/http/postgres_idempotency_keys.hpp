#pragma once

#include <userver/storages/postgres/transaction.hpp>

#include "application/ports/idempotency_keys.hpp"

namespace pdr::infrastructure::http {

/// Ключи идемпотентности в Postgres.
///
/// Сессией служит транзакция, в которой арендатор уже объявлен, — та же самая,
/// в которой идёт операция. Своей транзакции адаптер не открывает и открыть не
/// может: её ему передают, и в этом весь смысл (`ports::IdempotencyKeys`).
///
/// Пула здесь нет и упоминать его нечем: единственная дверь к соединениям —
/// `db::TenantContext`. Правило проверяет `scripts/check_layers.py`.
class PostgresIdempotencyKeys final
    : public pdr::http::ports::IdempotencyKeys<userver::storages::postgres::Transaction> {
public:
    PostgresIdempotencyKeys() = default;

private:
    core::Result<pdr::http::Claim> Take(userver::storages::postgres::Transaction& session,
                                        const core::TenantId& tenant,
                                        const pdr::http::IdempotencyKey& key,
                                        const pdr::http::RequestFingerprint& fingerprint,
                                        core::Instant expires_at) override;

    core::Result<void> Complete(userver::storages::postgres::Transaction& session,
                                const core::TenantId& tenant,
                                const pdr::http::IdempotencyKey& key,
                                const pdr::http::SavedAnswer& answer) override;
};

}  // namespace pdr::infrastructure::http
