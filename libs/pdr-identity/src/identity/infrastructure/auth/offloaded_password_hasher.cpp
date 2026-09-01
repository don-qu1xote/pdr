#include "identity/infrastructure/auth/offloaded_password_hasher.hpp"

#include <userver/engine/async.hpp>

namespace pdr::identity {

OffloadedPasswordHasher::OffloadedPasswordHasher(const ports::PasswordHasher& counting,
                                                 userver::engine::TaskProcessor& processor) noexcept
    : counting_{counting}, processor_{processor} {}

core::Result<PasswordHash> OffloadedPasswordHasher::Hash(const Password& password,
                                                         const PasswordRules& rules) const {
    return userver::engine::AsyncNoSpan(
               processor_, [this, &password, &rules] { return counting_.Hash(password, rules); })
        .Get();
}

bool OffloadedPasswordHasher::Matches(const Password& password, const PasswordHash& hash) const {
    return userver::engine::AsyncNoSpan(
               processor_, [this, &password, &hash] { return counting_.Matches(password, hash); })
        .Get();
}

}  // namespace pdr::identity
