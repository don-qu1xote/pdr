#include "billing/infrastructure/postgres_tariff_repository.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "core/money.hpp"

namespace pdr::billing {
namespace {

/// Условия по tenant_id в запросе нет намеренно: арендатора отсекает RLS в базе.
/// Забыть условие можно, обойти политику — нет; схема и политики заводятся
/// задачами области DB.
///
/// Запрос идёт в транзакции области, то есть на мастере: чтение доменных данных
/// живёт там, где объявлен арендатор. Реплика для него не годится — объявления
/// на ней не было бы.
const userver::storages::postgres::Query kFindByCode{
    "SELECT price_minor_units, currency_code FROM tariffs WHERE code = $1",
    userver::storages::postgres::Query::Name{"tariff_find_by_code"},
};

}  // namespace

PostgresTariffRepository::PostgresTariffRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<Tariff> PostgresTariffRepository::FindByCode(const TariffCode& code) const {
    const auto result = scope_.Session().Execute(kFindByCode, code.View());
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
