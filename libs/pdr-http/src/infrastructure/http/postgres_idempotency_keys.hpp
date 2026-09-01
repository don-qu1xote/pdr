#pragma once

#include "application/ports/idempotency_keys.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::infrastructure::http {

/// Ключи идемпотентности в Postgres.
///
/// Сессией служит область арендатора — та же самая, в которой идёт операция. Своей транзакции
/// адаптер не открывает и открыть не может: её ему передают, и в этом весь смысл
/// (`ports::IdempotencyKeys`).
///
/// Пула здесь нет и упоминать его нечем: единственная дверь к соединениям —
/// `db::TenantContext`. Правило проверяет `scripts/check_layers.py`.
class PostgresIdempotencyKeys final
    : public pdr::http::ports::IdempotencyKeys<db::ScopedTenantContext> {
public:
    PostgresIdempotencyKeys() = default;

private:
    core::Result<pdr::http::Claim> Take(db::ScopedTenantContext& session,
                                        const core::TenantId& tenant,
                                        const pdr::http::IdempotencyKey& key,
                                        const pdr::http::RequestFingerprint& fingerprint,
                                        core::Instant expires_at) override;

    core::Result<void> Complete(db::ScopedTenantContext& session,
                                const core::TenantId& tenant,
                                const pdr::http::IdempotencyKey& key,
                                const pdr::http::SavedAnswer& answer) override;
};

}  // namespace pdr::infrastructure::http
