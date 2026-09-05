#include "scheduling/infrastructure/postgres_recurrence_repository.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/io/date.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Filled;

/// Один участник серии. Порядок полей — порядок массивов в
/// db/sql/scheduling/scheduling_series_participants_add.sql: штатный
/// `ExecuteDecomposeBulk` раскладывает структуру по колонкам ровно так.
struct SeriesParticipantRow final {
    core::TenantId tenant_id;
    core::SeriesId series_id;
    core::PersonId participant_id;
};

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
        throw std::runtime_error{std::string{"scheduling_series."} + column + " не дата"};
    }
    return composed.Value();
}

core::LocalTime AtMinute(std::int16_t minute) {
    auto time = core::LocalTime::Compose(static_cast<unsigned>(minute) / 60,
                                         static_cast<unsigned>(minute) % 60);
    if (!time.HasValue()) {
        throw std::runtime_error{"scheduling_series.at_minute вне суток"};
    }
    return time.Value();
}

ExceptionKind AsKind(const std::string& text) {
    for (const auto kind : {ExceptionKind::kCancelled, ExceptionKind::kMoved}) {
        if (Name(kind) == text) {
            return kind;
        }
    }
    throw std::runtime_error{"scheduling_series_exception.kind вне закрытого списка: " + text};
}

}  // namespace

PostgresRecurrenceRepository::PostgresRecurrenceRepository(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

core::Result<void> PostgresRecurrenceRepository::Create(const RecurrenceSeries& series) {
    scope_.Session().Execute(sql::kSchedulingSeriesCreate,
                             series.Tenant(),
                             series.Id(),
                             series.Tutor(),
                             series.Rule().ToRRule(),
                             AsDate(series.StartsOn()),
                             static_cast<std::int16_t>(series.At().SinceMidnight().count()),
                             series.Zone().Name(),
                             static_cast<std::int32_t>(series.LessonDuration().count()));

    std::vector<SeriesParticipantRow> rows;
    rows.reserve(series.Participants().size());
    for (const auto& participant : series.Participants()) {
        rows.push_back(SeriesParticipantRow{series.Tenant(), series.Id(), participant});
    }
    if (!rows.empty()) {
        scope_.Session().ExecuteDecomposeBulk(sql::kSchedulingSeriesParticipantsAdd, rows);
    }

    return {};
}

std::optional<RecurrenceSeries> PostgresRecurrenceRepository::Find(const core::TenantId& tenant,
                                                                   const core::SeriesId& id) const {
    const auto found = scope_.Session().Execute(sql::kSchedulingSeriesFind, tenant, id);
    if (found.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        found.Front().As<SchedulingSeriesFindRow>(userver::storages::postgres::kRowTag);

    const auto tutor = core::PersonId::Parse(Filled(row.tutor_id, "tutor_id"));
    if (!tutor.has_value()) {
        throw std::runtime_error{"scheduling_series.tutor_id не идентификатор человека"};
    }
    auto zone = core::TimeZone::Parse(Filled(row.tz, "tz"));
    if (!zone.has_value()) {
        throw std::runtime_error{"scheduling_series.tz не имя зоны"};
    }
    auto rule = RecurrenceRule::Parse(Filled(row.rrule, "rrule"));
    if (!rule.HasValue()) {
        throw std::runtime_error{"scheduling_series.rrule не разбирается: " +
                                 rule.Failure().Code()};
    }

    const auto people = scope_.Session().Execute(sql::kSchedulingSeriesParticipantsOf, tenant, id);
    std::vector<core::PersonId> participants;
    participants.reserve(people.Size());
    for (const auto& raw : people) {
        const auto person =
            core::PersonId::Parse(Filled(raw.As<std::optional<std::string>>(), "participant_id"));
        if (!person.has_value()) {
            throw std::runtime_error{
                "scheduling_series_participant.participant_id не "
                "идентификатор человека"};
        }
        participants.push_back(*person);
    }

    auto series = RecurrenceSeries::Compose(
        id,
        tenant,
        *tutor,
        std::move(participants),
        rule.Value(),
        FromDate(Filled(row.starts_on, "starts_on"), "starts_on"),
        AtMinute(Filled(row.at_minute, "at_minute")),
        std::move(*zone),
        Lesson::Duration{Filled(row.duration_minutes, "duration_minutes")});
    if (!series.HasValue()) {
        throw std::runtime_error{"строка scheduling_series не собирается в серию: " +
                                 series.Failure().Code()};
    }

    const auto skipped = scope_.Session().Execute(sql::kSchedulingSeriesExceptionsOf, tenant, id);
    auto grown = series.Value();
    for (const auto& raw : skipped) {
        const auto one =
            raw.As<SchedulingSeriesExceptionsOfRow>(userver::storages::postgres::kRowTag);

        RecurrenceException exception{
            FromDate(Filled(one.occurrence_on, "occurrence_on"), "occurrence_on"),
            AsKind(Filled(one.kind, "kind"))};
        if (one.moved_to.has_value()) {
            exception.moved_to = AsInstant(*one.moved_to);
        }
        if (one.moved_minutes.has_value()) {
            exception.moved_duration = Lesson::Duration{*one.moved_minutes};
        }

        auto next = grown.With(std::move(exception));
        if (!next.HasValue()) {
            throw std::runtime_error{"строка scheduling_series_exception не ложится в серию: " +
                                     next.Failure().Code()};
        }
        grown = next.Value();
    }

    return grown;
}

core::Result<void> PostgresRecurrenceRepository::Record(const core::TenantId& tenant,
                                                        const core::SeriesId& id,
                                                        const RecurrenceException& exception) {
    std::optional<infrastructure::db::Timestamptz> moved;
    if (exception.moved_to.has_value()) {
        moved = AsTimestamptz(*exception.moved_to);
    }
    std::optional<std::int32_t> minutes;
    if (exception.moved_duration.has_value()) {
        minutes = static_cast<std::int32_t>(exception.moved_duration->count());
    }

    const auto written = scope_.Session().Execute(sql::kSchedulingSeriesExceptionRecord,
                                                  tenant,
                                                  id,
                                                  AsDate(exception.occurrence_on),
                                                  std::string{Name(exception.kind)},
                                                  moved,
                                                  minutes);
    if (written.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_exception_repeated",
                           "на это занятие уже есть исключение: отменено и перенесено "
                           "одновременно оно быть не может"};
    }

    return {};
}

}  // namespace pdr::scheduling
