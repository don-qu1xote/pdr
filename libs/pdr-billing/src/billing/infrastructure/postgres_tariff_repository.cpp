#include "billing/infrastructure/postgres_tariff_repository.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include "core/money.hpp"

namespace pdr::billing {
namespace {

/// Строка тарифа: тот же состав колонок и тот же порядок, что у
/// db/sql/billing/tariff_find_by_code.sql.
///
/// Написана руками, а не порождена: у запроса стоит `@no-dto` — таблицы
/// `tariffs` в схеме нет вовсе (docs/architecture/context-map.md, «Известные
/// расхождения с кодом»), и разбирать разборщику нечего. Читается всё равно
/// структурой: имя колонки строкой — это опечатка, которая находится в рантайме.
struct TariffRow final {
    std::int64_t price_minor_units{0};
    std::string currency_code;
};

}  // namespace

PostgresTariffRepository::PostgresTariffRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<Tariff> PostgresTariffRepository::FindByCode(const TariffCode& code) const {
    const auto result = scope_.Session().Execute(sql::kTariffFindByCode, code.View());
    if (result.IsEmpty())
        return std::nullopt;

    const auto row = result.Front().As<TariffRow>(userver::storages::postgres::kRowTag);
    const auto currency = core::CurrencyCode::Parse(row.currency_code);
    if (!currency.has_value()) {
        throw std::runtime_error{"tariffs.currency_code не является кодом валюты: " +
                                 row.currency_code};
    }

    return Tariff{code, core::Money::FromMinorUnits(row.price_minor_units, *currency)};
}

}  // namespace pdr::billing
