#include "identity/application/invite_participant.hpp"

namespace pdr::identity {

InviteParticipant::InviteParticipant(const ports::AuthSettings& settings,
                                     const ports::Digests& digests,
                                     ports::OneTimeTokens& tokens,
                                     const application::ports::IdGenerator& ids,
                                     const application::ports::SecretGenerator& secrets,
                                     const application::ports::Clock& clock) noexcept
    : settings_{settings},
      digests_{digests},
      tokens_{tokens},
      ids_{ids},
      secrets_{secrets},
      clock_{clock} {}

core::Result<IssuedInvitation> InviteParticipant::Execute(const core::TenantId& tenant,
                                                          Role role) const {
    const auto lifetimes = settings_.Lifetimes();
    if (!lifetimes) {
        return lifetimes.Failure();
    }

    const auto secret = TokenSecret::Parse(secrets_.NextText(kTokenBytes));
    if (!secret) {
        return secret.Failure();
    }

    auto token = OneTimeToken::Invitation(ids_.Next<TokenId>(),
                                          tenant,
                                          digests_.Of(secret.Value().Value()),
                                          role,
                                          clock_.Now(),
                                          lifetimes.Value().Invitation());
    if (!token) {
        return token.Failure();
    }

    tokens_.Issue(token.Value());
    return IssuedInvitation{token.Value(), secret.Value()};
}

}  // namespace pdr::identity
