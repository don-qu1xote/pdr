#include "billing/application/contract_service.hpp"

namespace pdr::billing {

ContractService::ContractService(const ports::TariffRepository& tariffs) noexcept
    : quote_{tariffs} {}

core::Result<core::Money> ContractService::QuotePackage(std::string_view tariff_code,
                                                        int lessons) const {
    const auto code = TariffCode::Parse(tariff_code);
    if (!code.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "tariff_code_invalid",
                           "код тарифа не похож на код тарифа"};
    }

    return quote_.Execute({*code, lessons});
}

}  // namespace pdr::billing
