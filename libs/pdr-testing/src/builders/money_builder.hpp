#pragma once

#include <cstdint>
#include <string_view>

#include "core/money.hpp"

namespace pdr::testing {

/// Билдер денег: `MoneyBuilder{}.Rubles(1500).Build()` вместо трёх строк с
/// разбором кода валюты и умножением на сто.
///
/// Билдер, а не фабричная функция на каждый случай: тесту оплат нужны и рубли, и
/// «ровно столько-то копеек», и чужая валюта для отрицательного случая, — а
/// новый тест не должен начинаться с подготовки на тридцать строк.
class MoneyBuilder final {
public:
    /// Рубли: сумма в основных единицах, копейки — нулевые.
    MoneyBuilder& Rubles(std::int64_t rubles) noexcept {
        minor_units_ = rubles * 100;
        currency_ = "RUB";
        return *this;
    }

    /// Минорные единицы напрямую: копейки, центы, тыйын.
    MoneyBuilder& Minor(std::int64_t minor_units) noexcept {
        minor_units_ = minor_units;
        return *this;
    }

    MoneyBuilder& Currency(std::string_view code) noexcept {
        currency_ = code;
        return *this;
    }

    /// Собрать. Код валюты обязан быть разборным: непроверенный код — это тест,
    /// который проверяет не то, что написано в его названии.
    core::Money Build() const {
        const auto currency = core::CurrencyCode::Parse(currency_);
        if (!currency.has_value()) {
            throw std::logic_error{"MoneyBuilder: код валюты не по ISO 4217"};
        }
        return core::Money::FromMinorUnits(minor_units_, *currency);
    }

private:
    std::int64_t minor_units_{0};
    std::string_view currency_{"RUB"};
};

}  // namespace pdr::testing
