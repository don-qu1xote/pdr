#pragma once

#include <string_view>

#include "billing/core/tariff.hpp"
#include "builders/money_builder.hpp"
#include "core/money.hpp"

namespace pdr::billing::testing {

/// Билдер тарифа: `TariffBuilder{}.Coded("MATH-EGE-90").PerLesson(2500).Build()`.
///
/// Лежит в модуле billing: доменный билдер живёт рядом со своим доменом, потому
/// что платформенная оснастка не имеет права зависеть от контекста
/// (docs/architecture/testing.md).
class TariffBuilder final {
public:
    TariffBuilder& Coded(std::string_view code) noexcept {
        code_ = code;
        return *this;
    }

    /// Цена занятия в рублях: копейки считает билдер денег, а не тест.
    TariffBuilder& PerLesson(std::int64_t rubles) noexcept {
        price_ = pdr::testing::MoneyBuilder{}.Rubles(rubles).Build();
        return *this;
    }

    TariffBuilder& PerLesson(core::Money price) noexcept {
        price_ = price;
        return *this;
    }

    Tariff Build() const {
        const auto code = TariffCode::Parse(code_);
        if (!code.has_value()) {
            throw std::logic_error{"TariffBuilder: код тарифа не по правилу"};
        }
        return Tariff{*code, price_};
    }

private:
    std::string_view code_{"MATH-EGE-90"};
    core::Money price_{pdr::testing::MoneyBuilder{}.Rubles(2500).Build()};
};

}  // namespace pdr::billing::testing
