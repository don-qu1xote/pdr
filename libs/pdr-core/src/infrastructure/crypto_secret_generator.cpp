#include "infrastructure/crypto_secret_generator.hpp"

#include <cstdint>
#include <cstring>
#include <tuple>

#include <userver/crypto/base64.hpp>
#include <userver/crypto/random.hpp>

namespace pdr::infrastructure {

std::string CryptoSecretGenerator::NextText(std::size_t bytes) const {
    return userver::crypto::base64::Base64UrlEncode(userver::crypto::GenerateRandomBlock(bytes),
                                                    userver::crypto::base64::Pad::kWithout);
}

core::IdBytes CryptoSecretGenerator::NextIdBytes() const {
    const auto block = userver::crypto::GenerateRandomBlock(std::tuple_size_v<core::IdBytes>);

    core::IdBytes bytes{};
    std::memcpy(bytes.data(), block.data(), bytes.size());

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    return bytes;
}

}  // namespace pdr::infrastructure
