#pragma once

#include <optional>

#include "identity/application/ports/session_store.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Сессии в базе, `identity_session`.
///
/// Арендатор из `SessionId` и арендатор области сверять здесь нечем — и не
/// нужно: если они разошлись, политика просто не покажет строку, и ответом
/// будет «такой сессии нет». Это ровно тот ответ, которого заслуживает
/// подсунутый чужой идентификатор.
class PostgresSessionStore final : public ports::SessionStore {
public:
    explicit PostgresSessionStore(infrastructure::db::ScopedTenantContext& scope) noexcept;

    void Save(const Session& session) override;

    std::optional<Session> Find(const SessionId& id) const override;

    void RevokeAllFor(const core::TenantId& tenant,
                      const core::PersonId& person,
                      core::Instant at) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
