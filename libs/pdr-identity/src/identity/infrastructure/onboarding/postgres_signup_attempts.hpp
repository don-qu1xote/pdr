#pragma once

#include <optional>

#include "identity/application/ports/signup_attempts.hpp"
#include "infrastructure/db/unscoped_access.hpp"

namespace pdr::identity {

/// Счётчик самостоятельных заведений поверх `identity_signup_attempt`.
///
/// Мимо области арендатора и по той же причине, что реестр записей: человек,
/// который заводится сам, ещё ни в какой практике не состоит.
class PostgresSignupAttempts final : public ports::SignupAttempts {
public:
    explicit PostgresSignupAttempts(const infrastructure::db::UnscopedAccess& access) noexcept;

    std::optional<AttemptWindow> Window(const Digest& address) const override;

    void Save(const Digest& address, const AttemptWindow& window) override;

private:
    const infrastructure::db::UnscopedAccess& access_;
};

}  // namespace pdr::identity
