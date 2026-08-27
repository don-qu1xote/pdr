#include "identity/core/account.hpp"

namespace pdr::identity {

core::Result<Account> Account::Registered(core::PersonId id,
                                          Digest mail,
                                          Digest confirmation,
                                          core::Instant at,
                                          core::Instant::Duration lifetime) {
    if (lifetime <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "confirmation_lifetime_not_positive",
                           "ссылка подтверждения нулевой длины срабатывает раньше, чем дойдёт"};
    }

    return Account{
        std::move(id), std::move(mail), std::nullopt, std::move(confirmation), at + lifetime, at};
}

Account Account::Invited(core::PersonId id, Digest mail, core::Instant at) noexcept {
    return Account{std::move(id), std::move(mail), at, std::nullopt, std::nullopt, at};
}

Account Account::Restore(core::PersonId id,
                         Digest mail,
                         std::optional<core::Instant> confirmed_at,
                         std::optional<Digest> confirmation,
                         std::optional<core::Instant> confirmation_expires_at,
                         core::Instant created_at) noexcept {
    return Account{std::move(id),
                   std::move(mail),
                   confirmed_at,
                   std::move(confirmation),
                   confirmation_expires_at,
                   created_at};
}

core::Result<Account> Account::ConfirmedBy(const Digest& shown, core::Instant at) const {
    if (IsConfirmed()) {
        return core::Error{
            core::ErrorKind::kConflict, "account_already_confirmed", "почта уже подтверждена"};
    }
    if (!confirmation_.has_value() || !(*confirmation_ == shown)) {
        return core::Error{
            core::ErrorKind::kNotFound, "confirmation_unknown", "такой ссылки подтверждения нет"};
    }
    if (confirmation_expires_at_.has_value() && at >= *confirmation_expires_at_) {
        return core::Error{
            core::ErrorKind::kConflict, "confirmation_expired", "срок ссылки подтверждения вышел"};
    }

    return Account{id_, mail_, at, std::nullopt, std::nullopt, created_at_};
}

core::Result<SignupLimits> SignupLimits::Compose(core::Instant::Duration window,
                                                 std::uint32_t per_address) {
    if (window <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "signup_window_not_positive",
                           "окно нулевой длины не считает ничего"};
    }
    if (per_address == 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "signup_limit_too_low",
                           "порог в ноль заведений запрещает регистрацию вовсе"};
    }

    return SignupLimits{window, per_address};
}

}  // namespace pdr::identity
