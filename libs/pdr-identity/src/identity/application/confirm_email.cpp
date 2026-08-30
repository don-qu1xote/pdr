#include "identity/application/confirm_email.hpp"

namespace pdr::identity {

ConfirmEmail::ConfirmEmail(const ports::Digests& digests,
                           ports::Accounts& accounts,
                           const application::ports::Clock& clock) noexcept
    : digests_{digests}, accounts_{accounts}, clock_{clock} {}

core::Result<Account> ConfirmEmail::Execute(const core::PersonId& account,
                                            const TokenSecret& shown) const {
    const auto found = accounts_.FindById(account);
    if (!found.has_value()) {
        return core::Error{
            core::ErrorKind::kNotFound, "account_unknown", "такой учётной записи нет"};
    }

    const auto confirmed = found->ConfirmedBy(digests_.Of(shown.Value()), clock_.Now());
    if (!confirmed) {
        return confirmed.Failure();
    }

    accounts_.Save(confirmed.Value());
    return confirmed.Value();
}

}  // namespace pdr::identity
