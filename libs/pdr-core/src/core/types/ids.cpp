#include "core/types/ids.hpp"

namespace pdr::core::detail {
namespace {

constexpr std::size_t kTextLength = 36;
constexpr std::array<std::size_t, 4> kHyphens{8, 13, 18, 23};

constexpr std::string_view kDigits = "0123456789abcdef";

std::optional<std::uint8_t> DigitValue(char symbol) noexcept {
    if (symbol >= '0' && symbol <= '9') {
        return static_cast<std::uint8_t>(symbol - '0');
    }
    if (symbol >= 'a' && symbol <= 'f') {
        return static_cast<std::uint8_t>(symbol - 'a' + 10);
    }
    if (symbol >= 'A' && symbol <= 'F') {
        return static_cast<std::uint8_t>(symbol - 'A' + 10);
    }
    return std::nullopt;
}

bool IsHyphenPosition(std::size_t position) noexcept {
    for (const std::size_t hyphen : kHyphens) {
        if (hyphen == position) {
            return true;
        }
    }
    return false;
}

}  // namespace

std::optional<IdBytes> ParseUuid(std::string_view text) {
    if (text.size() != kTextLength) {
        return std::nullopt;
    }

    IdBytes bytes{};
    std::size_t byte = 0;
    std::optional<std::uint8_t> high;

    for (std::size_t position = 0; position < text.size(); ++position) {
        if (IsHyphenPosition(position)) {
            if (text[position] != '-') {
                return std::nullopt;
            }
            continue;
        }

        const auto digit = DigitValue(text[position]);
        if (!digit.has_value()) {
            return std::nullopt;
        }
        if (!high.has_value()) {
            high = digit;
            continue;
        }
        bytes[byte++] = static_cast<std::uint8_t>((*high << 4) | *digit);
        high.reset();
    }

    return bytes;
}

std::string FormatUuid(const IdBytes& bytes) {
    std::string text;
    text.reserve(kTextLength);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            text.push_back('-');
        }
        text.push_back(kDigits[bytes[index] >> 4]);
        text.push_back(kDigits[bytes[index] & 0x0FU]);
    }
    return text;
}

std::size_t HashBytes(const IdBytes& bytes) noexcept {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(hash);
}

}  // namespace pdr::core::detail
