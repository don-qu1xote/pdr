#pragma once

#include <variant>

#include "application/ports/tariff_repository.hpp"
#include "core/money.hpp"
#include "core/tariff.hpp"

namespace pdr::application {

/// Сценарий: сколько стоит пакет из N занятий по такому-то тарифу.
///
/// Класс на сценарий, один публичный метод, зависимости — явными ссылками в
/// конструкторе. Ни синглтона, ни сервис-локатора: что сценарию нужно, видно из
/// его конструктора, и в тесте это заменяется фейком за одну строку.
class QuoteLessonPackage final {
public:
    struct Request final {
        core::TariffCode tariff_code;
        int lessons{0};
    };

    enum class Error {
        kTariffNotFound,
        kLessonsNotPositive,
        kPriceOverflow,
    };

    using Result = std::variant<core::Money, Error>;

    explicit QuoteLessonPackage(const ports::TariffRepository& tariffs) noexcept;

    Result Execute(const Request& request) const;

private:
    const ports::TariffRepository& tariffs_;
};

}  // namespace pdr::application
