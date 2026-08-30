#include "identity/infrastructure/auth/sha256_digests.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include <userver/crypto/hash.hpp>

namespace pdr::identity {

Digest Sha256Digests::Of(std::string_view text) const {
    auto hex = userver::crypto::hash::Sha256(text, userver::crypto::hash::OutputEncoding::kHex);

    std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });

    auto digest = Digest::Parse(hex);
    if (!digest) {
        throw std::runtime_error{"SHA-256 вернул не отпечаток: " + digest.Failure().Detail()};
    }
    return digest.Value();
}

}  // namespace pdr::identity
