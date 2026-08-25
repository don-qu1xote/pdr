#include "core/money.hpp"

#include <limits>

namespace pdr::core {
namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

bool AddOverflows(std::int64_t left, std::int64_t right) noexcept {
    if (right > 0 && left > kMax - right)
        return true;
    if (right < 0 && left < kMin - right)
        return true;
    return false;
}

bool MulOverflows(std::int64_t left, std::int64_t right) noexcept {
    if (left == 0 || right == 0)
        return false;
    if (left == -1)
        return right == kMin;
    if (right == -1)
        return left == kMin;
    if (left > 0) {
        return right > 0 ? left > kMax / right : right < kMin / left;
    }
    return right > 0 ? left < kMin / right : left < kMax / right;
}

bool IsUpperLatin(char symbol) noexcept {
    return symbol >= 'A' && symbol <= 'Z';
}

}  // namespace

std::optional<CurrencyCode> CurrencyCode::Parse(std::string_view text) {
    if (text.size() != 3)
        return std::nullopt;
    std::array<char, 3> letters{};
    for (std::size_t i = 0; i < letters.size(); ++i) {
        if (!IsUpperLatin(text[i]))
            return std::nullopt;
        letters[i] = text[i];
    }
    return CurrencyCode{letters};
}

Money Money::FromMinorUnits(std::int64_t minor_units, CurrencyCode currency) noexcept {
    return Money{minor_units, currency};
}

std::optional<Money> Money::Plus(const Money& other) const noexcept {
    if (currency_ != other.currency_)
        return std::nullopt;
    if (AddOverflows(minor_units_, other.minor_units_))
        return std::nullopt;
    return Money{minor_units_ + other.minor_units_, currency_};
}

std::optional<Money> Money::Minus(const Money& other) const noexcept {
    if (currency_ != other.currency_)
        return std::nullopt;
    if (other.minor_units_ == kMin)
        return std::nullopt;
    if (AddOverflows(minor_units_, -other.minor_units_))
        return std::nullopt;
    return Money{minor_units_ - other.minor_units_, currency_};
}

std::optional<Money> Money::Times(std::int64_t factor) const noexcept {
    if (MulOverflows(minor_units_, factor))
        return std::nullopt;
    return Money{minor_units_ * factor, currency_};
}

}  // namespace pdr::core
