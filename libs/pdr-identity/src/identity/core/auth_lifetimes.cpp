#include "identity/core/auth_lifetimes.hpp"

namespace pdr::identity {

core::Result<AuthLifetimes> AuthLifetimes::Compose(core::Instant::Duration session,
                                                   core::Instant::Duration invitation,
                                                   core::Instant::Duration password_reset) {
    const auto zero = core::Instant::Duration::zero();
    if (session <= zero || invitation <= zero || password_reset <= zero) {
        return core::Error{core::ErrorKind::kValidation,
                           "auth_lifetime_not_positive",
                           "срок, истёкший в момент выдачи, ничего не открывает"};
    }
    if (password_reset > invitation) {
        return core::Error{core::ErrorKind::kValidation,
                           "auth_reset_outlives_invitation",
                           "ссылка сброса живёт дольше приглашения: у неё риск выше, а срок "
                           "обязан быть короче"};
    }

    return AuthLifetimes{session, invitation, password_reset};
}

}  // namespace pdr::identity
