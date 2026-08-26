#include "identity/application/accept_invitation.hpp"

#include "identity/core/person.hpp"

namespace pdr::identity {

AcceptInvitation::AcceptInvitation(const ports::AuthSettings& settings,
                                   const ports::Digests& digests,
                                   const ports::PasswordHasher& hasher,
                                   ports::OneTimeTokens& tokens,
                                   ports::ParticipantDirectory& directory,
                                   ports::CredentialStore& credentials,
                                   ports::SessionStore& sessions,
                                   const application::ports::IdGenerator& ids,
                                   const application::ports::SecretGenerator& secrets,
                                   const application::ports::Clock& clock) noexcept
    : settings_{settings},
      digests_{digests},
      hasher_{hasher},
      tokens_{tokens},
      directory_{directory},
      credentials_{credentials},
      sessions_{sessions},
      ids_{ids},
      secrets_{secrets},
      clock_{clock} {}

core::Result<Session> AcceptInvitation::Execute(const AcceptInvitationRequest& request) const {
    const auto rules = settings_.Passwords();
    if (!rules) {
        return rules.Failure();
    }
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto now = clock_.Now();
    const auto found = tokens_.Find(request.tenant, digests_.Of(request.secret.Value()));
    if (!found.has_value() || found->Purpose() != TokenPurpose::kInvitation) {
        return core::Error{
            core::ErrorKind::kNotFound, "invitation_unknown", "такого приглашения нет"};
    }

    const auto used = found->Used(now);
    if (!used) {
        return used.Failure();
    }

    const auto chosen = Password::Chosen(request.password, rules.Value());
    if (!chosen) {
        return chosen.Failure();
    }
    const auto hash = hasher_.Hash(chosen.Value(), rules.Value());
    if (!hash) {
        return hash.Failure();
    }

    const Person person{ids_.Next<core::PersonId>(), request.mail, request.born_on};
    const ports::Enrolment enrolment{
        person, *found->InvitedAs(), request.display_name, request.zone};

    const auto enrolled = directory_.Enrol(request.tenant, enrolment);
    if (!enrolled) {
        return enrolled.Failure();
    }

    tokens_.MarkUsed(used.Value());
    credentials_.Save(request.tenant, person.Id(), hash.Value());

    auto opened = Session::Open(SessionId{request.tenant, secrets_.Next<SessionSecret>()},
                                person.Id(),
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
