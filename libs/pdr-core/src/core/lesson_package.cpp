#include "core/lesson_package.hpp"

namespace pdr::core {

Result<Money> PackagePrice(const Tariff& tariff, int lessons) {
    if (lessons <= 0) {
        return Error{ErrorKind::kValidation,
                     "lessons_not_positive",
                     "в пакете должно быть хотя бы одно занятие"};
    }

    const auto total = tariff.PricePerLesson().Times(static_cast<std::int64_t>(lessons));
    if (!total.has_value()) {
        return Error{ErrorKind::kValidation,
                     "package_price_overflow",
                     "столько занятий в пакет не помещается"};
    }

    return *total;
}

}  // namespace pdr::core
