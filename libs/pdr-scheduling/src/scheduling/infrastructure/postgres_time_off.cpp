#include "scheduling/infrastructure/postgres_time_off.hpp"

#include <chrono>
#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/date.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::Filled;

userver::storages::postgres::Date AsDate(const core::Date& date) {
    return userver::storages::postgres::Date{
        date.Year(), static_cast<int>(date.Month()), static_cast<int>(date.Day())};
}

core::Date FromDate(const userver::storages::postgres::Date& date, const char* column) {
    const std::chrono::year_month_day calendar{date.GetSysDays()};
    auto composed = core::Date::Compose(static_cast<int>(calendar.year()),
                                        static_cast<unsigned>(calendar.month()),
                                        static_cast<unsigned>(calendar.day()));
    if (!composed.HasValue()) {
        throw std::runtime_error{std::string{"scheduling_time_off."} + column + " не дата"};
    }
    return composed.Value();
}

}  // namespace

PostgresTimeOff::PostgresTimeOff(infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

core::Result<void> PostgresTimeOff::Save(const TimeOff& period, core::Instant declared_at) {
    std::optional<std::string> reason;
    if (period.Reason().has_value()) {
        reason = std::string{Name(*period.Reason())};
    }

    try {
        scope_.Session().Execute(sql::kSchedulingTimeOffDeclare,
                                 period.Tenant(),
                                 period.Id(),
                                 period.Person(),
                                 AsDate(period.From()),
                                 AsDate(period.To()),
                                 period.Zone().Name(),
                                 reason,
                                 period.DeclaredBy(),
                                 declared_at);
    } catch (const userver::storages::postgres::ExclusionViolation&) {
        return core::Error{core::ErrorKind::kConflict,
                           "time_off_overlaps",
                           "на эти дни у человека уже заведён перерыв"};
    }

    return {};
}

std::optional<TimeOff> PostgresTimeOff::Find(const core::TenantId& tenant,
                                             const core::TimeOffId& id) const {
    const auto found = scope_.Session().Execute(sql::kSchedulingTimeOffFind, tenant, id);
    if (found.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        found.Front().As<SchedulingTimeOffFindRow>(userver::storages::postgres::kRowTag);

    const auto person = core::PersonId::Parse(Filled(row.person_id, "person_id"));
    if (!person.has_value()) {
        throw std::runtime_error{"scheduling_time_off.person_id не идентификатор человека"};
    }
    const auto declared_by = core::PersonId::Parse(Filled(row.declared_by, "declared_by"));
    if (!declared_by.has_value()) {
        throw std::runtime_error{"scheduling_time_off.declared_by не идентификатор человека"};
    }
    auto zone = core::TimeZone::Parse(Filled(row.tz, "tz"));
    if (!zone.has_value()) {
        throw std::runtime_error{"scheduling_time_off.tz не имя зоны"};
    }

    /// Пустая причина — обычное дело, а не поломка строки: причина
    /// необязательна, и NULL здесь значит «не сказал», а не «не разобрали».
    std::optional<TimeOffReason> reason;
    if (row.reason.has_value()) {
        reason = ParseTimeOffReason(*row.reason);
        if (!reason.has_value()) {
            throw std::runtime_error{"scheduling_time_off.reason вне закрытого списка: " +
                                     *row.reason};
        }
    }

    auto period = TimeOff::Compose(id,
                                   tenant,
                                   *person,
                                   FromDate(Filled(row.from_date, "from_date"), "from_date"),
                                   FromDate(Filled(row.to_date, "to_date"), "to_date"),
                                   reason,
                                   std::move(*zone),
                                   *declared_by);
    if (!period.HasValue()) {
        throw std::runtime_error{"строка scheduling_time_off не собирается в перерыв: " +
                                 period.Failure().Code()};
    }
    return period.Value();
}

core::Result<void> PostgresTimeOff::Decide(const core::TenantId& tenant,
                                           const core::TimeOffId& id,
                                           TimeOffDecision lessons,
                                           SeriesDecision series,
                                           core::Instant at) {
    const auto written = scope_.Session().Execute(sql::kSchedulingTimeOffDecide,
                                                  tenant,
                                                  id,
                                                  std::string{Name(lessons)},
                                                  std::string{Name(series)},
                                                  at);
    if (written.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kConflict,
                           "time_off_already_decided",
                           "по этому перерыву решение уже принято: занятия отменены или "
                           "перенесены, и второй раз применять его не к чему"};
    }

    return {};
}

}  // namespace pdr::scheduling
