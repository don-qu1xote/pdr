#include "infrastructure/http/fingerprint.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>

#include <userver/crypto/hash.hpp>

namespace pdr::infrastructure::http {

pdr::http::RequestFingerprint FingerprintOf(std::string_view body) {
    auto hex = userver::crypto::hash::Sha256(body, userver::crypto::hash::OutputEncoding::kHex);

    std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char symbol) {
        return static_cast<char>(std::tolower(symbol));
    });

    auto fingerprint = pdr::http::RequestFingerprint::Parse(hex);
    if (!fingerprint) {
        throw std::runtime_error{"SHA-256 вернул не отпечаток: " + fingerprint.Failure().Detail()};
    }
    return fingerprint.Value();
}

}  // namespace pdr::infrastructure::http
