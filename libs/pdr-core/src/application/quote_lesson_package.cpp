#include "application/quote_lesson_package.hpp"

#include "core/lesson_package.hpp"

namespace pdr::application {

QuoteLessonPackage::QuoteLessonPackage(const ports::TariffRepository& tariffs) noexcept
    : tariffs_{tariffs} {}

core::Result<core::Money> QuoteLessonPackage::Execute(const Request& request) const {
    const auto tariff = tariffs_.FindByCode(request.tariff_code);
    if (!tariff.has_value()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "tariff_not_found",
                           "тарифа " + request.tariff_code.View() + " нет"};
    }

    return core::PackagePrice(*tariff, request.lessons);
}

}  // namespace pdr::application
