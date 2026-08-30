#include "identity/application/open_practice.hpp"

#include "identity/core/account.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/password.hpp"
#include "identity/core/person.hpp"
#include "identity/core/practice.hpp"
#include "identity/core/tenant.hpp"

namespace pdr::identity {

OpenPractice::OpenPractice(const ports::AuthSettings& settings,
                           const ports::Digests& digests,
                           const ports::PasswordHasher& hasher,
                           ports::Accounts& accounts,
                           ports::Practices& practices,
                           ports::ParticipantDirectory& directory,
                           ports::CredentialStore& credentials,
                           ports::SessionStore& sessions,
                           const application::ports::IdGenerator& ids,
                           const application::ports::SecretGenerator& secrets,
                           const application::ports::Clock& clock) noexcept
    : settings_{settings},
      digests_{digests},
      hasher_{hasher},
      accounts_{accounts},
      practices_{practices},
      directory_{directory},
      credentials_{credentials},
      sessions_{sessions},
      ids_{ids},
      secrets_{secrets},
      clock_{clock} {}

core::Result<OpenedPractice> OpenPractice::Execute(const OpenPracticeRequest& request) const {
    const auto rules = settings_.Passwords();
    if (!rules) {
        return rules.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto chosen = Password::Chosen(request.password, rules.Value());
    if (!chosen) {
        return chosen.Failure();
    }
    const auto hash = hasher_.Hash(chosen.Value(), rules.Value());
    if (!hash) {
        return hash.Failure();
    }

    const auto now = clock_.Now();
    const auto mail = digests_.Of(request.mail.Value());

    auto account = accounts_.FindByMail(mail);
    if (!account.has_value()) {
        account = Account::Invited(ids_.Next<core::PersonId>(), mail, now);
        accounts_.Save(*account);
    }

    auto practice = Tenant::Compose(ids_.Next<core::TenantId>(), request.practice_name);
    if (!practice) {
        return practice.Failure();
    }

    const auto opened_practice = Practice::Opened(practice.Value().Id(), now);
    const auto open = practices_.Open(practice.Value(), request.zone, opened_practice);
    if (!open) {
        return open.Failure();
    }

    const Person person{account->Id(), request.mail, request.born_on};
    const ports::Enrolment enrolment{
        person, RoleSet::Of({Role::kOwner, Role::kTutor}), request.display_name, request.zone};
    const auto enrolled = directory_.Enrol(practice.Value().Id(), enrolment);
    if (!enrolled) {
        return enrolled.Failure();
    }

    credentials_.Save(practice.Value().Id(), person.Id(), hash.Value());

    auto session = Session::Open(SessionId{practice.Value().Id(), secrets_.Next<SessionSecret>()},
                                 person.Id(),
                                 now,
                                 lifetimes.Value().Session(),
                                 request.seen);
    if (!session) {
        return session.Failure();
    }

    sessions_.Save(session.Value());
    return OpenedPractice{practice.Value().Id(), person.Id(), session.Value()};
}

}  // namespace pdr::identity
