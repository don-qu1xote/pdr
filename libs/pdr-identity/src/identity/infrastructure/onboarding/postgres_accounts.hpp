#pragma once

#include <optional>

#include "identity/application/ports/accounts.hpp"
#include "infrastructure/db/unscoped_access.hpp"

namespace pdr::identity {

/// Реестр учётных записей поверх `identity_account`.
///
/// ХОДИТ МИМО ОБЛАСТИ АРЕНДАТОРА — через вторую дверь, `UnscopedAccess`, с
/// названной причиной. Пула соединений он при этом не называет: имя причины
/// стоит в конструкторе, и обход виден на ревью, а не прячется за обычным
/// словом.
///
/// Работать иначе он не может по существу: вопрос «этот человек уже есть на
/// площадке?» задаётся до того, как известен арендатор, и часто до того, как
/// арендатор вообще появился.
class PostgresAccounts final : public ports::Accounts {
public:
    explicit PostgresAccounts(const infrastructure::db::UnscopedAccess& access) noexcept;

    std::optional<Account> FindByMail(const Digest& mail) const override;

    std::optional<Account> FindById(const core::PersonId& id) const override;

    void Save(const Account& account) override;

private:
    const infrastructure::db::UnscopedAccess& access_;
};

}  // namespace pdr::identity
