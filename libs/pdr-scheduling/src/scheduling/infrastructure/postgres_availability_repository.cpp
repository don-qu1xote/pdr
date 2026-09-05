#include "scheduling/infrastructure/postgres_availability_repository.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/io/date.hpp>

#include "application/ports/id_generator.hpp"
#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Filled;

using AvailabilityId = core::StrongId<struct AvailabilityTag>;

/// Одно недельное правило. Порядок полей — порядок массивов в
/// db/sql/scheduling/scheduling_availability_add.sql.
struct RuleRow final {
    core::TenantId tenant_id;
    AvailabilityId id;
    core::PersonId tutor_id;
    std::int16_t weekday{};
    std::int16_t from_minute{};
    std::int16_t to_minute{};
    std::string tz;
};

/// Одно исключение. Порядок полей — порядок массивов в
/// db/sql/scheduling/scheduling_availability_exceptions_add.sql.
struct ExceptionRow final {
    core::TenantId tenant_id;
    core::PersonId tutor_id;
    userver::storages::postgres::Date on_date;
    std::optional<infrastructure::db::Timestamptz> starts_at;
    std::optional<infrastructure::db::Timestamptz> ends_at;
};

std::int16_t Minutes(core::LocalTime time) noexcept {
    return static_cast<std::int16_t>(time.SinceMidnight().count());
}

core::LocalTime AtMinute(std::int16_t minute) {
    auto time = core::LocalTime::Compose(static_cast<unsigned>(minute) / 60,
                                         static_cast<unsigned>(minute) % 60);
    if (!time.HasValue()) {
        throw std::runtime_error{"scheduling_availability: минута вне суток"};
    }
    return time.Value();
}

userver::storages::postgres::Date AsDate(const core::Date& date) {
    return userver::storages::postgres::Date{
        date.Year(), static_cast<int>(date.Month()), static_cast<int>(date.Day())};
}

core::Date FromDate(const userver::storages::postgres::Date& date) {
    const std::chrono::year_month_day calendar{date.GetSysDays()};
    auto composed = core::Date::Compose(static_cast<int>(calendar.year()),
                                        static_cast<unsigned>(calendar.month()),
                                        static_cast<unsigned>(calendar.day()));
    if (!composed.HasValue()) {
        throw std::runtime_error{"scheduling_availability_exception.on_date не дата"};
    }
    return composed.Value();
}

}  // namespace

PostgresAvailabilityRepository::PostgresAvailabilityRepository(
    infrastructure::db::ScopedTenantContext& scope,
    const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

std::optional<Availability> PostgresAvailabilityRepository::Of(const core::TenantId& tenant,
                                                               const core::PersonId& tutor) const {
    const auto found = scope_.Session().Execute(sql::kSchedulingAvailabilityOfTutor, tenant, tutor);
    const auto skipped =
        scope_.Session().Execute(sql::kSchedulingAvailabilityExceptionsOfTutor, tenant, tutor);
    if (found.IsEmpty() && skipped.IsEmpty()) {
        return std::nullopt;
    }

    std::vector<AvailabilityRule> rules;
    rules.reserve(found.Size());
    for (const auto& raw : found) {
        const auto row =
            raw.As<SchedulingAvailabilityOfTutorRow>(userver::storages::postgres::kRowTag);
        auto zone = core::TimeZone::Parse(Filled(row.tz, "tz"));
        if (!zone.has_value()) {
            throw std::runtime_error{"scheduling_availability.tz не имя зоны"};
        }
        auto rule =
            AvailabilityRule::Compose(static_cast<core::Weekday>(Filled(row.weekday, "weekday")),
                                      AtMinute(Filled(row.from_minute, "from_minute")),
                                      AtMinute(Filled(row.to_minute, "to_minute")),
                                      std::move(*zone));
        if (!rule.HasValue()) {
            throw std::runtime_error{"строка scheduling_availability не собирается в правило: " +
                                     rule.Failure().Code()};
        }
        rules.push_back(rule.Value());
    }

    std::vector<AvailabilityException> exceptions;
    exceptions.reserve(skipped.Size());
    for (const auto& raw : skipped) {
        const auto row = raw.As<SchedulingAvailabilityExceptionsOfTutorRow>(
            userver::storages::postgres::kRowTag);
        std::optional<core::TimeRange> instead;
        if (row.starts_at.has_value() && row.ends_at.has_value()) {
            auto span =
                core::TimeRange::Compose(AsInstant(*row.starts_at), AsInstant(*row.ends_at));
            if (!span.HasValue()) {
                throw std::runtime_error{"scheduling_availability_exception: отрезок вывернут"};
            }
            instead = span.Value();
        }
        exceptions.push_back(
            AvailabilityException{FromDate(Filled(row.on_date, "on_date")), instead});
    }

    auto availability = Availability::Compose(std::move(rules), std::move(exceptions));
    if (!availability.HasValue()) {
        throw std::runtime_error{"строки доступности не собираются: " +
                                 availability.Failure().Code()};
    }
    return availability.Value();
}

core::Result<void> PostgresAvailabilityRepository::Replace(const core::TenantId& tenant,
                                                           const core::PersonId& tutor,
                                                           const Availability& availability) {
    scope_.Session().Execute(sql::kSchedulingAvailabilityClear, tenant, tutor);
    scope_.Session().Execute(sql::kSchedulingAvailabilityExceptionsClear, tenant, tutor);

    std::vector<RuleRow> rules;
    rules.reserve(availability.Rules().size());
    for (const auto& rule : availability.Rules()) {
        rules.push_back(RuleRow{tenant,
                                ids_.Next<AvailabilityId>(),
                                tutor,
                                static_cast<std::int16_t>(rule.Day()),
                                Minutes(rule.From()),
                                Minutes(rule.To()),
                                rule.Zone().Name()});
    }
    if (!rules.empty()) {
        scope_.Session().ExecuteDecomposeBulk(sql::kSchedulingAvailabilityAdd, rules);
    }

    std::vector<ExceptionRow> exceptions;
    exceptions.reserve(availability.Exceptions().size());
    for (const auto& exception : availability.Exceptions()) {
        std::optional<infrastructure::db::Timestamptz> from;
        std::optional<infrastructure::db::Timestamptz> to;
        if (exception.instead.has_value()) {
            from = AsTimestamptz(exception.instead->From());
            to = AsTimestamptz(exception.instead->To());
        }
        exceptions.push_back(ExceptionRow{tenant, tutor, AsDate(exception.date), from, to});
    }
    if (!exceptions.empty()) {
        scope_.Session().ExecuteDecomposeBulk(sql::kSchedulingAvailabilityExceptionsAdd,
                                              exceptions);
    }

    return {};
}

}  // namespace pdr::scheduling
