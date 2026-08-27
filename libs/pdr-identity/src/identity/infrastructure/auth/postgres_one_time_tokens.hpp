#pragma once

#include <optional>

#include "identity/application/ports/one_time_tokens.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Одноразовые токены в базе, `identity_one_time_token`.
///
/// Поиск идёт по отпечатку — самого секрета в этих запросах нет ни в одном
/// параметре, и положить его в базу нечем.
class PostgresOneTimeTokens final : public ports::OneTimeTokens {
public:
    explicit PostgresOneTimeTokens(infrastructure::db::ScopedTenantContext& scope) noexcept;

    void Issue(const OneTimeToken& token) override;

    std::optional<OneTimeToken> Find(const core::TenantId& tenant,
                                     const Digest& secret) const override;

    std::optional<OneTimeToken> LiveInvitationTo(const core::TenantId& tenant,
                                                 const Digest& invited,
                                                 core::Instant now) const override;

    void MarkUsed(const OneTimeToken& token) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
