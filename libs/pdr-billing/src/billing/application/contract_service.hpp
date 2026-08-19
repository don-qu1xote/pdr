#pragma once

#include <string_view>

#include "billing/application/quote_lesson_package.hpp"
#include "billing/contract.hpp"

namespace pdr::billing {

/// Реализация публичного контракта billing: разбирает сырые значения на своей
/// границе и отдаёт работу сценарию.
class ContractService final : public Contract {
public:
    explicit ContractService(const ports::TariffRepository& tariffs) noexcept;

    core::Result<core::Money> QuotePackage(std::string_view tariff_code,
                                           int lessons) const override;

private:
    QuoteLessonPackage quote_;
};

}  // namespace pdr::billing
