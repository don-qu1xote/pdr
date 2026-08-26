#pragma once

#include <optional>

#include "identity/application/ports/credential_store.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Пароли в базе, `identity_credential`.
///
/// Строится от области арендатора: пул в этом заголовке не упоминается вовсе, и
/// сходить за чужим хешем мимо арендатора нечем (`scripts/check_layers.py`).
///
/// Условия по арендатору в запросах нет — его ставит политика. Поиск по почте
/// идёт джойном с `identity_person`: пароль лежит отдельно от человека, потому
/// что человек без пароля — обычное дело.
class PostgresCredentialStore final : public ports::CredentialStore {
public:
    explicit PostgresCredentialStore(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::optional<ports::Credential> FindByEmail(const core::TenantId& tenant,
                                                 const Email& mail) const override;

    std::optional<PasswordHash> FindFor(const core::TenantId& tenant,
                                        const core::PersonId& person) const override;

    void Save(const core::TenantId& tenant,
              const core::PersonId& person,
              const PasswordHash& hash) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
