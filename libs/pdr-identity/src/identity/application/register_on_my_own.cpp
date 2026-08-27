#include "identity/application/register_on_my_own.hpp"

#include "identity/application/invite_participant.hpp"

namespace pdr::identity {

RegisterOnMyOwn::RegisterOnMyOwn(const ports::AuthSettings& settings,
                                 const ports::Digests& digests,
                                 ports::Accounts& accounts,
                                 ports::SignupAttempts& attempts,
                                 const application::ports::IdGenerator& ids,
                                 const application::ports::SecretGenerator& secrets,
                                 const application::ports::Clock& clock) noexcept
    : settings_{settings},
      digests_{digests},
      accounts_{accounts},
      attempts_{attempts},
      ids_{ids},
      secrets_{secrets},
      clock_{clock} {}

core::Result<SelfRegistration> RegisterOnMyOwn::Execute(const Email& mail,
                                                        const Digest& from) const {
    const auto limits = settings_.Signups();
    if (!limits) {
        return limits.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto now = clock_.Now();
    const auto window = attempts_.Window(from);
    if (window.has_value() &&
        window->IsBlockedAt(now, limits.Value().Window(), limits.Value().PerAddress())) {
        return core::Error{core::ErrorKind::kConflict,
                           "signup_throttled",
                           "с этого адреса слишком много заведений подряд"};
    }

    const auto counted = window.has_value() ? window->Registered(now, limits.Value().Window())
                                            : AttemptWindow::Restore(now, 1);
    attempts_.Save(from, counted);

    const auto digest = digests_.Of(mail.Value());
    if (accounts_.FindByMail(digest).has_value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "account_already_exists",
                           "учётная запись с такой почтой уже есть"};
    }

    const auto secret = TokenSecret::Parse(secrets_.NextText(kTokenBytes));
    if (!secret) {
        return secret.Failure();
    }

    const auto account = Account::Registered(ids_.Next<core::PersonId>(),
                                             digest,
                                             digests_.Of(secret.Value().Value()),
                                             now,
                                             lifetimes.Value().PasswordReset());
    if (!account) {
        return account.Failure();
    }

    accounts_.Save(account.Value());
    return SelfRegistration{account.Value(), secret.Value()};
}

}  // namespace pdr::identity
