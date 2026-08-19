#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace pdr::core {

/// Код валюты по ISO 4217: ровно три заглавные латинские буквы.
/// Значение либо разобрано, либо его нет — «пустой» или «неизвестной» валюты не
/// существует.
class CurrencyCode final {
public:
    static std::optional<CurrencyCode> Parse(std::string_view text);

    std::string_view View() const noexcept {
        return {letters_.data(), letters_.size()};
    }

    friend bool operator==(const CurrencyCode&, const CurrencyCode&) = default;

private:
    explicit CurrencyCode(std::array<char, 3> letters) noexcept : letters_{letters} {}

    std::array<char, 3> letters_{};
};

/// Деньги — целое число минорных единиц и код валюты. Числа с плавающей точкой
/// в этот тип не входят и никогда не войдут: сложение копеек не должно зависеть
/// от округления двоичной дроби.
///
/// Арифметика возвращает std::optional: разные валюты и переполнение — обычные
/// ответы «так нельзя», а не аварии. Решает вызывающий.
class Money final {
public:
    static Money FromMinorUnits(std::int64_t minor_units, CurrencyCode currency) noexcept;

    std::int64_t MinorUnits() const noexcept {
        return minor_units_;
    }
    const CurrencyCode& Currency() const noexcept {
        return currency_;
    }

    std::optional<Money> Plus(const Money& other) const noexcept;
    std::optional<Money> Minus(const Money& other) const noexcept;
    std::optional<Money> Times(std::int64_t factor) const noexcept;

    friend bool operator==(const Money&, const Money&) = default;

private:
    Money(std::int64_t minor_units, CurrencyCode currency) noexcept
        : minor_units_{minor_units}, currency_{currency} {}

    std::int64_t minor_units_{0};
    CurrencyCode currency_;
};

static_assert(sizeof(std::int64_t) == 8, "минорные единицы считаются в 64-битном целом");

}  // namespace pdr::core
