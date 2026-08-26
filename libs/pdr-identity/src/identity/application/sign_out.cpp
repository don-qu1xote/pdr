#include "identity/application/sign_out.hpp"

namespace pdr::identity {

SignOut::SignOut(ports::SessionStore& sessions, const application::ports::Clock& clock) noexcept
    : sessions_{sessions}, clock_{clock} {}

core::Result<void> SignOut::Execute(const SessionId& id) const {
    const auto found = sessions_.Find(id);
    if (!found.has_value()) {
        return {};
    }

    sessions_.Save(found->Revoked(clock_.Now()));
    return {};
}

}  // namespace pdr::identity
