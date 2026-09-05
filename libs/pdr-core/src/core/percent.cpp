#include "core/percent.hpp"

#include <limits>

namespace pdr::core {

std::optional<Percent> Percent::Compose(int value) noexcept {
    if (value < 0 || value > kWhole) {
        return std::nullopt;
    }
    return Percent{value};
}

Percent Percent::Nothing() noexcept {
    return Percent{0};
}

std::optional<Money> Percent::Of(const Money& amount) const noexcept {
    const auto minor = amount.MinorUnits();
    if (minor > std::numeric_limits<std::int64_t>::max() / kWhole ||
        minor < std::numeric_limits<std::int64_t>::min() / kWhole) {
        return std::nullopt;
    }

    return Money::FromMinorUnits(minor * value_ / kWhole, amount.Currency());
}

}  // namespace pdr::core
