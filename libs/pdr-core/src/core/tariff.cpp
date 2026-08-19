#include "core/tariff.hpp"

namespace pdr::core {
namespace {

constexpr std::size_t kMinLength = 3;
constexpr std::size_t kMaxLength = 32;

bool IsAllowed(char symbol) noexcept {
    const bool letter = symbol >= 'A' && symbol <= 'Z';
    const bool digit = symbol >= '0' && symbol <= '9';
    return letter || digit || symbol == '-';
}

}  // namespace

std::optional<TariffCode> TariffCode::Parse(std::string_view text) {
    if (text.size() < kMinLength || text.size() > kMaxLength)
        return std::nullopt;
    if (text.front() == '-' || text.back() == '-')
        return std::nullopt;
    for (const char symbol : text) {
        if (!IsAllowed(symbol))
            return std::nullopt;
    }
    return TariffCode{std::string{text}};
}

}  // namespace pdr::core
