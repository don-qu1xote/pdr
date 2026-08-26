#include "identity/application/change_password.hpp"

#include "identity/application/authenticate_session.hpp"

namespace pdr::identity {

ChangePassword::ChangePassword(const ports::AuthSettings& settings,
                               ports::CredentialStore& credentials,
                               const ports::PasswordHasher& hasher,
                               ports::SessionStore& sessions,
                               const application::ports::SecretGenerator& secrets,
                               const application::ports::Clock& clock) noexcept
    : settings_{settings},
      credentials_{credentials},
      hasher_{hasher},
      sessions_{sessions},
      secrets_{secrets},
      clock_{clock} {}

core::Result<Session> ChangePassword::Execute(const ChangePasswordRequest& request) const {
    const auto rules = settings_.Passwords();
    if (!rules) {
        return rules.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const AuthenticateSession authenticated{sessions_, clock_};
    const auto session = authenticated.Execute(request.current);
    if (!session) {
        return session.Failure();
    }

    const auto& tenant = session.Value().Tenant();
    const auto& person = session.Value().Person();

    const auto stored = credentials_.FindFor(tenant, person);
    if (!stored.has_value()) {
        return core::Error{
            core::ErrorKind::kNotFound, "password_not_set", "менять нечего: пароль ещё не задан"};
    }

    const auto given = Password::Given(request.old_secret);
    if (!given) {
        return given.Failure();
    }
    if (!hasher_.Matches(given.Value(), *stored)) {
        return core::Error{
            core::ErrorKind::kValidation, "password_old_mismatch", "прежний пароль не подошёл"};
    }

    const auto chosen = Password::Chosen(request.new_secret, rules.Value());
    if (!chosen) {
        return chosen.Failure();
    }

    const auto hash = hasher_.Hash(chosen.Value(), rules.Value());
    if (!hash) {
        return hash.Failure();
    }

    const auto now = clock_.Now();
    credentials_.Save(tenant, person, hash.Value());
    sessions_.RevokeAllFor(tenant, person, now);

    auto opened = Session::Open(SessionId{tenant, secrets_.Next<SessionSecret>()},
                                person,
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
