#include "identity/core/birth_date.hpp"

namespace pdr::identity {
namespace {

constexpr int kEarliestYear = 1900;
constexpr int kLatestYear = 2200;

void AppendPadded(std::string& text, unsigned value, std::size_t width) {
    auto digits = std::to_string(value);
    text.append(width > digits.size() ? width - digits.size() : 0, '0');
    text.append(digits);
}

}  // namespace

core::Result<BirthDate> BirthDate::Of(int year, unsigned month, unsigned day) {
    const std::chrono::year_month_day value{
        std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};

    if (!value.ok()) {
        return core::Error{
            core::ErrorKind::kValidation, "birth_date_invalid", "такой даты не бывает в календаре"};
    }

    if (year < kEarliestYear || year > kLatestYear) {
        return core::Error{
            core::ErrorKind::kValidation, "birth_date_invalid", "год рождения похож на опечатку"};
    }

    return BirthDate{value};
}

std::string BirthDate::ToString() const {
    std::string text;
    text.reserve(10);
    AppendPadded(text, static_cast<unsigned>(Year()), 4);
    text.push_back('-');
    AppendPadded(text, Month(), 2);
    text.push_back('-');
    AppendPadded(text, Day(), 2);
    return text;
}

}  // namespace pdr::identity
