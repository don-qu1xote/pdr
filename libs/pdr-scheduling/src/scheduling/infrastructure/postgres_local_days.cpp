#include "scheduling/infrastructure/postgres_local_days.hpp"

#include <optional>
#include <stdexcept>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/io/date.hpp>

#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Timestamptz;

/// Строка ответа руками: запрос помечен `@no-dto`, и порождать её не из чего —
/// разборщику негде взять образцовое имя зоны, а без него `at time zone`
/// отказывает прямо в разборе.
struct SpanRow final {
    Timestamptz from_at;
    Timestamptz to_at;
};

userver::storages::postgres::Date AsDate(const core::Date& date) {
    return userver::storages::postgres::Date{
        date.Year(), static_cast<int>(date.Month()), static_cast<int>(date.Day())};
}

}  // namespace

PostgresLocalDays::PostgresLocalDays(infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

core::Result<core::TimeRange> PostgresLocalDays::Between(const core::Date& from,
                                                         const core::Date& to,
                                                         const core::TimeZone& zone) const {
    if (to < from) {
        return core::Error{core::ErrorKind::kValidation,
                           "local_days_backwards",
                           "конец отрезка дней раньше его начала"};
    }

    /// ИМЯ ЗОНЫ ПРОВЕРЯЕТ БАЗА, И ОТКАЗ ЕЁ — ОБЫЧНОЕ ЗНАЧЕНИЕ. `TimeZone::Parse`
    /// проверяет ФОРМУ имени, а не существование зоны: списка зон у ядра нет.
    /// «Europe/Mordor» проходит форму и не проходит здесь — и это ответ
    /// человеку, а не авария процесса.
    ///
    /// `DataException` — весь класс 22 SQLSTATE, и незнакомая зона приходит
    /// именно им (22023). Ловится класс, а не отдельный код: перечислять коды
    /// значило бы держать здесь копию их списка.
    try {
        const auto found = scope_.Session().Execute(
            sql::kSchedulingLocalDaysSpan, AsDate(from), AsDate(to), zone.Name());
        if (found.IsEmpty()) {
            throw std::runtime_error{"scheduling_local_days_span не вернул строки"};
        }

        const auto row = found.Front().As<SpanRow>(userver::storages::postgres::kRowTag);
        const auto span = core::TimeRange::Compose(AsInstant(row.from_at), AsInstant(row.to_at));
        if (!span.HasValue()) {
            throw std::runtime_error{"отрезок дней не собирается в промежуток: " +
                                     span.Failure().Code()};
        }
        return span.Value();
    } catch (const userver::storages::postgres::DataException&) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_zone_unknown",
                           "такой часовой зоны нет: проверьте название"};
    }
}

}  // namespace pdr::scheduling
