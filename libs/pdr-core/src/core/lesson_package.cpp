#include "core/lesson_package.hpp"

namespace pdr::core {

PackagePriceResult PackagePrice(const Tariff& tariff, int lessons) {
    if (lessons <= 0)
        return PackagePriceError::kLessonsNotPositive;

    const auto total = tariff.PricePerLesson().Times(static_cast<std::int64_t>(lessons));
    if (!total.has_value())
        return PackagePriceError::kOverflow;

    return *total;
}

}  // namespace pdr::core
