#include "billing/core/lesson_package.hpp"

namespace pdr::billing {

core::Result<core::Money> PackagePrice(const Tariff& tariff, int lessons) {
    if (lessons <= 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "lessons_not_positive",
                           "в пакете должно быть хотя бы одно занятие"};
    }

    const auto total = tariff.PricePerLesson().Times(static_cast<std::int64_t>(lessons));
    if (!total.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "package_price_overflow",
                           "столько занятий в пакет не помещается"};
    }

    return *total;
}

}  // namespace pdr::billing
