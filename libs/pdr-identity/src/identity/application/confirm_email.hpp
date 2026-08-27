#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/accounts.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/core/account.hpp"
#include "identity/core/one_time_token.hpp"

namespace pdr::identity {

/// Подтвердить почту по ссылке из письма.
///
/// Второе средство антифрода после порога частоты, и вместе они закрывают
/// дешёвый случай: адрес, которого не существует, дальше этого шага не
/// проходит. Дорогой случай — живой человек с живым адресом и чужим именем —
/// закрывается не здесь, а разбором на публикации профиля.
class ConfirmEmail final {
public:
    ConfirmEmail(const ports::Digests& digests,
                 ports::Accounts& accounts,
                 const application::ports::Clock& clock) noexcept;

    core::Result<Account> Execute(const core::PersonId& account, const TokenSecret& shown) const;

private:
    const ports::Digests& digests_;
    ports::Accounts& accounts_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
