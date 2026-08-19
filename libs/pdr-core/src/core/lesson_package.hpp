#pragma once

#include <variant>

#include "core/money.hpp"
#include "core/tariff.hpp"

namespace pdr::core {

enum class PackagePriceError {
    kLessonsNotPositive,  ///< пакет из нуля занятий — не пакет
    kOverflow,  ///< столько денег не бывает; считать дальше нечестно
};

using PackagePriceResult = std::variant<Money, PackagePriceError>;

/// Правило: пакет стоит ровно столько, сколько занятий в нём, умноженных на цену
/// занятия. Ни скидок, ни округлений здесь нет — появятся, когда появятся, и
/// тоже правилом, а не догадкой на месте вызова.
///
/// Верхней границы числа занятий тут намеренно нет: «не больше N занятий в
/// пакете» — число, влияющее на людей, и жить оно должно в динамическом
/// конфиге, а не константой в домене.
PackagePriceResult PackagePrice(const Tariff& tariff, int lessons);

}  // namespace pdr::core
