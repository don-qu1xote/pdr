#include "identity/infrastructure/access/postgres_birth_dates.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/io/date.hpp>

namespace pdr::identity {
PostgresBirthDates::PostgresBirthDates(infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::optional<BirthDate> PostgresBirthDates::Of(const core::TenantId&,
                                                const core::PersonId& person) const {
    const auto result = scope_.Session().Execute(sql::kIdentityPersonBornOn, person.ToString());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto stored = result.Front().As<std::optional<userver::storages::postgres::Date>>(
        userver::storages::postgres::kFieldTag);
    if (!stored.has_value()) {
        return std::nullopt;
    }

    const std::chrono::year_month_day civil{
        std::chrono::floor<std::chrono::days>(stored->GetSysDays())};
    auto born = BirthDate::Of(static_cast<int>(civil.year()),
                              static_cast<unsigned>(civil.month()),
                              static_cast<unsigned>(civil.day()));
    if (!born) {
        throw std::runtime_error{"identity_person.born_on не дата: " + born.Failure().Detail()};
    }
    return born.Value();
}

}  // namespace pdr::identity
