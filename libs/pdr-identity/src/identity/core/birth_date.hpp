#pragma once

#include <chrono>
#include <string>

#include "core/errors.hpp"

namespace pdr::identity {

/// Дата рождения — значение, а не поле «возраст».
///
/// Возраст здесь не хранится намеренно: ученик взрослеет во время пользования
/// продуктом, и число, посчитанное однажды, начинает врать на следующий день
/// рождения — вместе со всеми правами, которые от него зависят. Возраст
/// спрашивают у момента: `AgeStatus::At(born_on, now)`.
///
/// Проверяется календарь, а не возраст: тридцатого февраля не бывает, а вот
/// «сколько лет — много или мало» этот тип не знает и знать не должен.
class BirthDate final {
public:
    static core::Result<BirthDate> Of(int year, unsigned month, unsigned day);

    std::chrono::year_month_day Value() const noexcept {
        return value_;
    }

    int Year() const noexcept {
        return static_cast<int>(value_.year());
    }
    unsigned Month() const noexcept {
        return static_cast<unsigned>(value_.month());
    }
    unsigned Day() const noexcept {
        return static_cast<unsigned>(value_.day());
    }

    /// «2011-03-04»: тот же вид, в котором дату отдаёт и принимает база.
    std::string ToString() const;

    friend bool operator==(const BirthDate&, const BirthDate&) = default;

private:
    explicit BirthDate(std::chrono::year_month_day value) noexcept : value_{value} {}

    std::chrono::year_month_day value_{};
};

}  // namespace pdr::identity
