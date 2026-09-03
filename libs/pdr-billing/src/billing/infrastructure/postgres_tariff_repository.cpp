#include "billing/infrastructure/postgres_tariff_repository.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include "core/money.hpp"

namespace pdr::billing {
PostgresTariffRepository::PostgresTariffRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<Tariff> PostgresTariffRepository::FindByCode(const TariffCode& code) const {
    const auto result = scope_.Session().Execute(sql::kTariffFindByCode, code.View());
    if (result.IsEmpty())
        return std::nullopt;

    const auto row = result.Front();
    const auto minor_units = row["price_minor_units"].As<std::int64_t>();
    const auto currency_text = row["currency_code"].As<std::string>();

    const auto currency = core::CurrencyCode::Parse(currency_text);
    if (!currency.has_value()) {
        throw std::runtime_error{"tariffs.currency_code не является кодом валюты: " +
                                 currency_text};
    }

    return Tariff{code, core::Money::FromMinorUnits(minor_units, *currency)};
}

}  // namespace pdr::billing
