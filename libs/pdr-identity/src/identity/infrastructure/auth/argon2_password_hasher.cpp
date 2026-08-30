#include "identity/infrastructure/auth/argon2_password_hasher.hpp"

#include <cstdint>
#include <cstring>
#include <string>

#include <argon2.h>

namespace pdr::identity {

Argon2PasswordHasher::Argon2PasswordHasher(
    const application::ports::SecretGenerator& secrets) noexcept
    : secrets_{secrets} {}

core::Result<PasswordHash> Argon2PasswordHasher::Hash(const Password& password,
                                                      const PasswordRules& rules) const {
    const auto salt = secrets_.NextText(kSaltBytes);

    const auto length = argon2_encodedlen(rules.Iterations(),
                                          rules.MemoryKib(),
                                          rules.Parallelism(),
                                          static_cast<std::uint32_t>(salt.size()),
                                          kHashBytes,
                                          Argon2_id);

    std::string encoded(length, '\0');
    const auto outcome = argon2id_hash_encoded(rules.Iterations(),
                                               rules.MemoryKib(),
                                               rules.Parallelism(),
                                               password.Secret().data(),
                                               password.Secret().size(),
                                               salt.data(),
                                               salt.size(),
                                               kHashBytes,
                                               encoded.data(),
                                               encoded.size());
    if (outcome != ARGON2_OK) {
        return core::Error{core::ErrorKind::kValidation,
                           "password_hashing_failed",
                           std::string{argon2_error_message(outcome)}};
    }

    encoded.resize(std::strlen(encoded.c_str()));
    return PasswordHash::Parse(encoded);
}

bool Argon2PasswordHasher::Matches(const Password& password, const PasswordHash& hash) const {
    return argon2id_verify(hash.Value().c_str(),
                           password.Secret().data(),
                           password.Secret().size()) == ARGON2_OK;
}

}  // namespace pdr::identity
