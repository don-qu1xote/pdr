#include "identity/application/authenticate_session.hpp"

namespace pdr::identity {

AuthenticateSession::AuthenticateSession(const ports::SessionStore& sessions,
                                         const application::ports::Clock& clock) noexcept
    : sessions_{sessions}, clock_{clock} {}

core::Result<Session> AuthenticateSession::Execute(const SessionId& id) const {
    const auto found = sessions_.Find(id);
    if (!found.has_value()) {
        return core::Error{core::ErrorKind::kNotFound, "session_unknown", "такой сессии нет"};
    }

    if (found->RevokedAt().has_value()) {
        return core::Error{core::ErrorKind::kForbidden, "session_revoked", "сессия отозвана"};
    }
    if (!found->IsUsableAt(clock_.Now())) {
        return core::Error{core::ErrorKind::kForbidden, "session_expired", "срок сессии вышел"};
    }

    return *found;
}

}  // namespace pdr::identity
