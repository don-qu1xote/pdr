#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "core/money.hpp"

namespace pdr::billing {

/// Код тарифа — то, чем репетитор называет свою услугу в договорённостях с
/// учеником: «MATH-EGE-90». Это доменное значение, а не технический
/// идентификатор записи.
class TariffCode final {
public:
    static std::optional<TariffCode> Parse(std::string_view text);

    const std::string& View() const noexcept {
        return text_;
    }

    friend bool operator==(const TariffCode&, const TariffCode&) = default;

private:
    explicit TariffCode(std::string text) noexcept : text_{std::move(text)} {}

    std::string text_;
};

/// Тариф: во сколько репетитор оценил одно занятие.
class Tariff final {
public:
    Tariff(TariffCode code, core::Money price_per_lesson) noexcept
        : code_{std::move(code)}, price_per_lesson_{price_per_lesson} {}

    const TariffCode& Code() const noexcept {
        return code_;
    }
    const core::Money& PricePerLesson() const noexcept {
        return price_per_lesson_;
    }

private:
    TariffCode code_;
    core::Money price_per_lesson_;
};

}  // namespace pdr::billing
