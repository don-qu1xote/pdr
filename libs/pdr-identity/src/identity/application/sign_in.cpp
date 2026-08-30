#include "identity/application/sign_in.hpp"

#include <utility>

namespace pdr::identity {
namespace {

/// Один и тот же отказ на все неудачи входа. Разные коды здесь означали бы,
/// что подбор пароля начинается с бесплатного вопроса «а есть ли такой».
core::Error Refused() {
    return core::Error{
        core::ErrorKind::kValidation, "sign_in_refused", "почта или пароль не подошли"};
}

core::Error Throttled() {
    return core::Error{
        core::ErrorKind::kConflict, "sign_in_throttled", "слишком много попыток подряд"};
}

}  // namespace

Digest AccountFingerprint(const ports::Digests& digests,
                          const core::TenantId& tenant,
                          const Email& mail) {
    return digests.Of(tenant.ToString() + "\n" + mail.Value());
}

SignIn::SignIn(const ports::AuthSettings& settings,
               const ports::CredentialStore& credentials,
               const ports::PasswordHasher& hasher,
               const ports::Digests& digests,
               ports::LoginAttempts& attempts,
               ports::SessionStore& sessions,
               const application::ports::SecretGenerator& secrets,
               const application::ports::Clock& clock) noexcept
    : settings_{settings},
      credentials_{credentials},
      hasher_{hasher},
      digests_{digests},
      attempts_{attempts},
      sessions_{sessions},
      secrets_{secrets},
      clock_{clock} {}

core::Result<Session> SignIn::Execute(const SignInRequest& request) const {
    const auto rules = settings_.Passwords();
    if (!rules) {
        return rules.Failure();
    }
    const auto limits = settings_.Throttle();
    if (!limits) {
        return limits.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto now = clock_.Now();
    const auto window = limits.Value().Window();
    const auto account = AccountFingerprint(digests_, request.tenant, request.mail);
    const auto& address = request.seen.Address();

    const auto counters = {std::pair{AttemptSubject::kAccount, account},
                           std::pair{AttemptSubject::kAddress, address}};
    for (const auto& [subject, of] : counters) {
        if (attempts_.Seen(request.tenant, subject, of)
                .IsBlockedAt(now, window, limits.Value().For(subject))) {
            return Throttled();
        }
    }

    const auto given = Password::Given(request.secret);
    const auto found = credentials_.FindByEmail(request.tenant, request.mail);

    const auto matched =
        given.HasValue() && found.has_value() && hasher_.Matches(given.Value(), found->hash);
    if (!found.has_value() && given.HasValue()) {
        static_cast<void>(hasher_.Hash(given.Value(), rules.Value()));
    }

    if (!matched) {
        for (const auto& [subject, of] : counters) {
            attempts_.Register(request.tenant, subject, of, now, window);
        }
        return Refused();
    }

    for (const auto& [subject, of] : counters) {
        attempts_.Forget(request.tenant, subject, of);
    }

    if (request.current.has_value()) {
        const auto previous = sessions_.Find(*request.current);
        if (previous.has_value()) {
            sessions_.Save(previous->Revoked(now));
        }
    }

    auto opened = Session::Open(SessionId{request.tenant, secrets_.Next<SessionSecret>()},
                                found->person,
                                now,
                                lifetimes.Value().Session(),
                                request.seen);
    if (!opened) {
        return opened.Failure();
    }

    sessions_.Save(opened.Value());
    return opened.Value();
}

}  // namespace pdr::identity
