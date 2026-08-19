#include "application/quote_lesson_package.hpp"

#include "core/lesson_package.hpp"

namespace pdr::application {

QuoteLessonPackage::QuoteLessonPackage(const ports::TariffRepository& tariffs) noexcept
    : tariffs_{tariffs} {}

QuoteLessonPackage::Result QuoteLessonPackage::Execute(const Request& request) const {
    const auto tariff = tariffs_.FindByCode(request.tariff_code);
    if (!tariff.has_value())
        return Error::kTariffNotFound;

    const auto price = core::PackagePrice(*tariff, request.lessons);
    if (const auto* money = std::get_if<core::Money>(&price))
        return *money;

    switch (std::get<core::PackagePriceError>(price)) {
        case core::PackagePriceError::kLessonsNotPositive:
            return Error::kLessonsNotPositive;
        case core::PackagePriceError::kOverflow:
            return Error::kPriceOverflow;
    }

    return Error::kPriceOverflow;  // недостижимо: перечисление разобрано целиком
}

}  // namespace pdr::application
