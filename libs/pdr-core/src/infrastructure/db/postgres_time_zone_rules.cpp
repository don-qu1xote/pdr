#include "infrastructure/db/postgres_time_zone_rules.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/exceptions.hpp>

#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::infrastructure::db {
namespace {

/// Строка ответа руками: запрос помечен `@no-dto`, и порождать её не из чего —
/// разборщику негде взять образцовое имя зоны.
struct TransitionRow final {
    Timestamptz at;
    std::int64_t offset_seconds{};
};

}  // namespace

PostgresTimeZoneRules::PostgresTimeZoneRules(ScopedTenantContext& scope) noexcept : scope_{scope} {}

core::Result<core::ZoneOffsets> PostgresTimeZoneRules::For(const core::TimeZone& zone,
                                                           const core::TimeRange& covering) const {
    userver::storages::postgres::ResultSet found{nullptr};
    try {
        found = scope_.Session().Execute(
            sql::kZoneTransitions, covering.From(), covering.To(), zone.Name());
    } catch (const userver::storages::postgres::DataException&) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_zone_unknown",
                           "такой часовой зоны нет: проверьте название"};
    }

    if (found.IsEmpty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_zone_rules_empty",
                           "база не назвала ни одного смещения для этой зоны"};
    }

    core::Instant::Duration initial{};
    std::vector<core::OffsetShift> shifts;
    shifts.reserve(found.Size() - 1);

    bool first = true;
    for (const auto& raw : found) {
        const auto row = raw.As<TransitionRow>(userver::storages::postgres::kRowTag);
        const auto offset = std::chrono::duration_cast<core::Instant::Duration>(
            std::chrono::seconds{row.offset_seconds});
        if (first) {
            initial = offset;
            first = false;
            continue;
        }
        shifts.push_back(core::OffsetShift{AsInstant(row.at), offset});
    }

    return core::ZoneOffsets::Compose(initial, std::move(shifts));
}

}  // namespace pdr::infrastructure::db
