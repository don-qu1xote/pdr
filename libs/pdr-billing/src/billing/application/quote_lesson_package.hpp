#pragma once

#include "billing/application/ports/tariff_repository.hpp"
#include "billing/core/tariff.hpp"
#include "core/errors.hpp"
#include "core/money.hpp"

namespace pdr::billing {

/// Сценарий: сколько стоит пакет из N занятий по такому-то тарифу.
///
/// Класс на сценарий, один публичный метод, зависимости — явными ссылками в
/// конструкторе. Ни синглтона, ни сервис-локатора: что сценарию нужно, видно из
/// его конструктора, и в тесте это заменяется фейком за одну строку.
class QuoteLessonPackage final {
public:
    struct Request final {
        TariffCode tariff_code;
        int lessons{0};
    };

    explicit QuoteLessonPackage(const ports::TariffRepository& tariffs) noexcept;

    /// Отказ домена возвращается как есть: сценарий не переписывает его своими
    /// словами и не превращает в исключение.
    core::Result<core::Money> Execute(const Request& request) const;

private:
    const ports::TariffRepository& tariffs_;
};

}  // namespace pdr::billing
