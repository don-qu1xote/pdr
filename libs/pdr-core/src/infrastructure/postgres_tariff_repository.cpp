#include "billing/infrastructure/postgres_tariff_repository.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>

#include <userver/storages/postgres/query.hpp>

#include "core/money.hpp"

namespace pdr::billing {
namespace {

/// Условия по tenant_id в запросе нет намеренно: арендатора отсекает RLS в базе.
/// Забыть условие можно, обойти политику — нет; схема и политики заводятся
/// задачами области DB.
const userver::storages::postgres::Query kFindByCode{
    "SELECT price_minor_units, currency_code FROM tariffs WHERE code = $1",
    userver::storages::postgres::Query::Name{"tariff_find_by_code"},
};

}  // namespace

PostgresTariffRepository::PostgresTariffRepository(userver::storages::postgres::ClusterPtr cluster)
    : cluster_{std::move(cluster)} {}

std::optional<Tariff> PostgresTariffRepository::FindByCode(const TariffCode& code) const {
    const auto result = cluster_->Execute(
        userver::storages::postgres::ClusterHostType::kSlave, kFindByCode, code.View());
    if (result.IsEmpty())
        return std::nullopt;

    const auto row = result.Front();
    const auto minor_units = row["price_minor_units"].As<std::int64_t>();
    const auto currency_text = row["currency_code"].As<std::string>();

    const auto currency = core::CurrencyCode::Parse(currency_text);
    if (!currency.has_value()) {
        // Хранилище отдало то, чего в домене не бывает. Это поломка данных, а не
        // «тариф не найден», и заминать её нельзя.
        throw std::runtime_error{"tariffs.currency_code не является кодом валюты: " +
                                 currency_text};
    }

    return Tariff{code, core::Money::FromMinorUnits(minor_units, *currency)};
}

}  // namespace pdr::billing
