#pragma once

#include "identity/application/ports/login_attempts.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Счётчики попыток входа в базе, `identity_login_attempt`.
///
/// ПРИБАВЛЕНИЕ И ЧТЕНИЕ — ОДИН ЗАПРОС. `insert ... on conflict do update
/// ... returning` неделим: две реплики, обрабатывающие две попытки
/// одновременно, дают два, а не один. Счётчик, который читают, а потом пишут,
/// теряет попытки ровно тогда, когда их больше всего.
///
/// Истёкшее окно начинается заново прямо в запросе — тем же условием, что и в
/// домене (`AttemptWindow::Registered`). Отдельной уборки для этого не нужно:
/// запрет снимается сам.
///
/// «Попыток не было» отдельным состоянием не выражается: `Seen` отдаёт окно с
/// нулём попыток, а такое не запирает ни при каком пороге.
class PostgresLoginAttempts final : public ports::LoginAttempts {
public:
    explicit PostgresLoginAttempts(infrastructure::db::ScopedTenantContext& scope) noexcept;

    AttemptWindow Register(const core::TenantId& tenant,
                           AttemptSubject subject,
                           const Digest& of,
                           core::Instant at,
                           core::Instant::Duration window) override;

    AttemptWindow Seen(const core::TenantId& tenant,
                       AttemptSubject subject,
                       const Digest& of) const override;

    void Forget(const core::TenantId& tenant, AttemptSubject subject, const Digest& of) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
