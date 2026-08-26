#include "identity/core/session.hpp"

namespace pdr::identity {
namespace {

constexpr char kSeparator = '.';

}  // namespace

core::Result<SessionId> SessionId::Parse(std::string_view text) {
    const auto split = text.find(kSeparator);
    if (split == std::string_view::npos) {
        return core::Error{core::ErrorKind::kValidation,
                           "session_id_malformed",
                           "идентификатор сессии записывается как «арендатор.секрет»"};
    }

    const auto tenant = core::TenantId::Parse(text.substr(0, split));
    const auto secret = SessionSecret::Parse(text.substr(split + 1));
    if (!tenant.has_value() || !secret.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "session_id_malformed",
                           "обе части идентификатора сессии — идентификаторы"};
    }

    return SessionId{*tenant, *secret};
}

std::string SessionId::ToString() const {
    return tenant_.ToString() + kSeparator + secret_.ToString();
}

core::Result<Session> Session::Open(SessionId id,
                                    core::PersonId person,
                                    core::Instant at,
                                    core::Instant::Duration lifetime,
                                    Fingerprint seen) {
    if (lifetime <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "session_lifetime_not_positive",
                           "сессия, негодная в момент выдачи, — это не сессия"};
    }

    return Session{
        std::move(id), std::move(person), at, at + lifetime, std::nullopt, std::move(seen)};
}

Session Session::Restore(SessionId id,
                         core::PersonId person,
                         core::Instant created_at,
                         core::Instant expires_at,
                         std::optional<core::Instant> revoked_at,
                         Fingerprint seen) {
    return Session{
        std::move(id), std::move(person), created_at, expires_at, revoked_at, std::move(seen)};
}

bool Session::IsUsableAt(core::Instant moment) const noexcept {
    if (revoked_at_.has_value()) {
        return false;
    }
    return moment < expires_at_;
}

Session Session::Revoked(core::Instant at) const {
    if (revoked_at_.has_value()) {
        return *this;
    }

    return Session{id_, person_, created_at_, expires_at_, at, seen_};
}

}  // namespace pdr::identity
