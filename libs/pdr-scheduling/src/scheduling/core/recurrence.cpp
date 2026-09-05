#include "scheduling/core/recurrence.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>

namespace pdr::scheduling {
namespace {

constexpr std::array<std::string_view, 7> kDayCodes{"SU", "MO", "TU", "WE", "TH", "FR", "SA"};

constexpr std::string_view kSupported =
    "поддержаны FREQ=WEEKLY, INTERVAL, BYDAY и одно из COUNT или UNTIL";

std::optional<core::Weekday> DayFromCode(std::string_view code) {
    for (std::size_t index = 0; index < kDayCodes.size(); ++index) {
        if (kDayCodes[index] == code) {
            return static_cast<core::Weekday>(index);
        }
    }
    return std::nullopt;
}

std::string_view CodeOfDay(core::Weekday day) {
    const auto index = static_cast<std::size_t>(day);
    return index < kDayCodes.size() ? kDayCodes[index] : kDayCodes.front();
}

std::optional<int> WholeNumber(std::string_view text) {
    if (text.empty() || text.size() > 9) {
        return std::nullopt;
    }
    int value = 0;
    for (const char symbol : text) {
        if (symbol < '0' || symbol > '9') {
            return std::nullopt;
        }
        value = value * 10 + (symbol - '0');
    }
    return value;
}

std::optional<core::Date> DateFromUntil(std::string_view text) {
    if (text.size() < 8) {
        return std::nullopt;
    }
    const auto year = WholeNumber(text.substr(0, 4));
    const auto month = WholeNumber(text.substr(4, 2));
    const auto day = WholeNumber(text.substr(6, 2));
    if (!year.has_value() || !month.has_value() || !day.has_value()) {
        return std::nullopt;
    }
    if (text.size() > 8 && text.substr(8) != "T000000Z") {
        return std::nullopt;
    }

    auto date =
        core::Date::Compose(*year, static_cast<unsigned>(*month), static_cast<unsigned>(*day));
    if (!date.HasValue()) {
        return std::nullopt;
    }
    return date.Value();
}

std::vector<std::string_view> SplitBy(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto found = text.find(separator, start);
        const auto end = found == std::string_view::npos ? text.size() : found;
        parts.push_back(text.substr(start, end - start));
        if (found == std::string_view::npos) {
            break;
        }
        start = found + 1;
    }
    return parts;
}

core::Error Unsupported(std::string_view part) {
    return core::Error{
        core::ErrorKind::kValidation,
        "recurrence_rule_unsupported",
        "правило повторения содержит «" + std::string{part} + "»: " + std::string{kSupported}};
}

/// Местная дата вхождения, если сдвинуться от начала серии на столько недель и
/// встать на этот день недели.
core::Date DateOf(const core::Date& week_start, core::Weekday day, int weeks) {
    const std::chrono::year_month_day base{std::chrono::year{week_start.Year()},
                                           std::chrono::month{week_start.Month()},
                                           std::chrono::day{week_start.Day()}};
    const auto shifted = std::chrono::sys_days{base} + std::chrono::weeks{weeks} +
                         std::chrono::days{static_cast<int>(day)};
    const std::chrono::year_month_day moved{shifted};
    return core::Date::Compose(static_cast<int>(moved.year()),
                               static_cast<unsigned>(moved.month()),
                               static_cast<unsigned>(moved.day()))
        .Value();
}

/// Воскресенье той недели, в которую попадает дата. Неделя начинается с
/// воскресенья, потому что с него же начинается нумерация `core::Weekday`.
core::Date WeekStartOf(const core::Date& date) {
    const std::chrono::year_month_day given{std::chrono::year{date.Year()},
                                            std::chrono::month{date.Month()},
                                            std::chrono::day{date.Day()}};
    const auto back = std::chrono::days{static_cast<int>(date.DayOfWeek())};
    const std::chrono::year_month_day start{std::chrono::sys_days{given} - back};
    return core::Date::Compose(static_cast<int>(start.year()),
                               static_cast<unsigned>(start.month()),
                               static_cast<unsigned>(start.day()))
        .Value();
}

core::Date DayBefore(const core::Date& date) {
    const std::chrono::year_month_day given{std::chrono::year{date.Year()},
                                            std::chrono::month{date.Month()},
                                            std::chrono::day{date.Day()}};
    const std::chrono::year_month_day earlier{std::chrono::sys_days{given} - std::chrono::days{1}};
    return core::Date::Compose(static_cast<int>(earlier.year()),
                               static_cast<unsigned>(earlier.month()),
                               static_cast<unsigned>(earlier.day()))
        .Value();
}

const RecurrenceException* ExceptionOn(const std::vector<RecurrenceException>& exceptions,
                                       const core::Date& date) {
    for (const auto& exception : exceptions) {
        if (exception.occurrence_on == date) {
            return &exception;
        }
    }
    return nullptr;
}

}  // namespace

std::string_view Name(ExceptionKind kind) noexcept {
    switch (kind) {
        case ExceptionKind::kCancelled:
            return "cancelled";
        case ExceptionKind::kMoved:
            return "moved";
        case ExceptionKind::kBoundary:
            break;
    }
    return "cancelled";
}

std::string_view Name(Placement placement) noexcept {
    switch (placement) {
        case Placement::kExact:
            return "exact";
        case Placement::kMovedByHand:
            return "moved_by_hand";
        case Placement::kMissingAfterClockChange:
            return "missing_after_clock_change";
        case Placement::kTwiceOnTheClock:
            return "twice_on_the_clock";
        case Placement::kBoundary:
            break;
    }
    return "exact";
}

core::Result<RecurrenceRule> RecurrenceRule::Compose(int interval,
                                                     std::vector<core::Weekday> days,
                                                     Ending ending) {
    if (interval < 1 || interval > kMaxInterval) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_interval_out_of_range",
                           "повторять реже, чем раз в двенадцать недель, — это уже не серия"};
    }
    if (days.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_days_empty",
                           "не сказано, по каким дням недели идут занятия"};
    }

    std::sort(days.begin(), days.end());
    if (std::adjacent_find(days.begin(), days.end()) != days.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_day_repeated",
                           "один и тот же день недели назван дважды"};
    }
    if (std::find(days.begin(), days.end(), core::Weekday::kBoundary) != days.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_day_out_of_week",
                           "такого дня недели не бывает"};
    }

    if (const auto* count = std::get_if<Count>(&ending)) {
        if (count->times < 1 || count->times > kMaxCount) {
            return core::Error{core::ErrorKind::kValidation,
                               "recurrence_count_out_of_range",
                               "занятий в серии должно быть от одного до четырёхсот"};
        }
    }

    return RecurrenceRule{interval, std::move(days), std::move(ending)};
}

core::Result<RecurrenceRule> RecurrenceRule::Parse(std::string_view rrule) {
    if (rrule.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_rule_empty",
                           "правило повторения не написано"};
    }

    int interval = 1;
    std::vector<core::Weekday> days;
    std::optional<Ending> ending;
    bool frequency_seen = false;

    for (const auto part : SplitBy(rrule, ';')) {
        if (part.empty()) {
            continue;
        }
        const auto equals = part.find('=');
        if (equals == std::string_view::npos) {
            return Unsupported(part);
        }
        const auto name = part.substr(0, equals);
        const auto value = part.substr(equals + 1);

        if (name == "FREQ") {
            if (value != "WEEKLY") {
                return Unsupported(part);
            }
            frequency_seen = true;
            continue;
        }
        if (name == "INTERVAL") {
            const auto found = WholeNumber(value);
            if (!found.has_value()) {
                return Unsupported(part);
            }
            interval = *found;
            continue;
        }
        if (name == "BYDAY") {
            for (const auto code : SplitBy(value, ',')) {
                const auto day = DayFromCode(code);
                if (!day.has_value()) {
                    return Unsupported(part);
                }
                days.push_back(*day);
            }
            continue;
        }
        if (name == "COUNT" || name == "UNTIL") {
            if (ending.has_value()) {
                return core::Error{
                    core::ErrorKind::kValidation,
                    "recurrence_two_endings",
                    "серия кончается либо счётом занятий, либо датой, но не тем и другим сразу"};
            }
            if (name == "COUNT") {
                const auto found = WholeNumber(value);
                if (!found.has_value()) {
                    return Unsupported(part);
                }
                ending = Ending{Count{*found}};
            } else {
                const auto found = DateFromUntil(value);
                if (!found.has_value()) {
                    return Unsupported(part);
                }
                ending = Ending{Until{*found}};
            }
            continue;
        }

        return Unsupported(part);
    }

    if (!frequency_seen) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_frequency_missing",
                           "не сказано, как часто повторяются занятия: " + std::string{kSupported}};
    }
    if (!ending.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_no_ending",
                           "серия без конца развернётся до горизонта у каждого запроса: назовите "
                           "число занятий или дату окончания"};
    }

    return Compose(interval, std::move(days), std::move(*ending));
}

std::string RecurrenceRule::ToRRule() const {
    std::string written = "FREQ=WEEKLY";
    if (interval_ != 1) {
        written += ";INTERVAL=" + std::to_string(interval_);
    }

    written += ";BYDAY=";
    for (std::size_t index = 0; index < days_.size(); ++index) {
        if (index > 0) {
            written += ",";
        }
        written += std::string{CodeOfDay(days_[index])};
    }

    if (const auto* count = std::get_if<Count>(&ending_)) {
        written += ";COUNT=" + std::to_string(count->times);
        return written;
    }

    const auto& until = std::get<Until>(ending_).date;
    const auto padded = [](unsigned value) {
        return (value < 10 ? std::string{"0"} : std::string{}) + std::to_string(value);
    };
    written +=
        ";UNTIL=" + std::to_string(until.Year()) + padded(until.Month()) + padded(until.Day());
    return written;
}

RecurrenceSeries::RecurrenceSeries(core::SeriesId id,
                                   core::TenantId tenant,
                                   core::PersonId tutor,
                                   std::vector<core::PersonId> participants,
                                   RecurrenceRule rule,
                                   core::Date starts_on,
                                   core::LocalTime at,
                                   core::TimeZone zone,
                                   Lesson::Duration duration,
                                   std::vector<RecurrenceException> exceptions)
    : id_{std::move(id)},
      tenant_{std::move(tenant)},
      tutor_{std::move(tutor)},
      participants_{std::move(participants)},
      rule_{std::move(rule)},
      starts_on_{starts_on},
      at_{at},
      zone_{std::move(zone)},
      duration_{duration},
      exceptions_{std::move(exceptions)} {}

core::Result<RecurrenceSeries> RecurrenceSeries::Compose(core::SeriesId id,
                                                         core::TenantId tenant,
                                                         core::PersonId tutor,
                                                         std::vector<core::PersonId> participants,
                                                         RecurrenceRule rule,
                                                         core::Date starts_on,
                                                         core::LocalTime at,
                                                         core::TimeZone zone,
                                                         Lesson::Duration duration) {
    if (duration <= Lesson::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_duration_not_positive",
                           "занятие нулевой длины — не занятие"};
    }
    if (participants.size() != Lesson::kParticipantsForNow) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_participants_not_one",
                           "групповые занятия ещё не заведены: участник сегодня ровно один"};
    }
    if (std::find(participants.begin(), participants.end(), tutor) != participants.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_tutor_among_participants",
                           "репетитор ведёт занятие, а не участвует в нём"};
    }
    if (const auto* until = std::get_if<Until>(&rule.Ends()); until != nullptr) {
        if (until->date < starts_on) {
            return core::Error{core::ErrorKind::kValidation,
                               "recurrence_ends_before_it_starts",
                               "серия кончается раньше, чем начинается"};
        }
    }

    return RecurrenceSeries{std::move(id),
                            std::move(tenant),
                            std::move(tutor),
                            std::move(participants),
                            std::move(rule),
                            starts_on,
                            at,
                            std::move(zone),
                            duration,
                            {}};
}

core::Result<RecurrenceSeries> RecurrenceSeries::With(RecurrenceException exception) const {
    if (exception.kind == ExceptionKind::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_exception_kind_unknown",
                           "такого вида исключения не бывает"};
    }
    if (exception.kind == ExceptionKind::kMoved && !exception.moved_to.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_move_without_place",
                           "перенос без нового времени — это отмена, и называться должен так же"};
    }
    if (exception.kind == ExceptionKind::kCancelled && exception.moved_to.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_cancel_with_place",
                           "у отмены нового времени не бывает"};
    }
    if (exception.moved_duration.has_value() &&
        *exception.moved_duration <= Lesson::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "lesson_duration_not_positive",
                           "занятие нулевой длины — не занятие"};
    }
    if (ExceptionOn(exceptions_, exception.occurrence_on) != nullptr) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_exception_repeated",
                           "на это занятие уже есть исключение: отменено и перенесено "
                           "одновременно оно быть не может"};
    }

    auto grown = exceptions_;
    grown.push_back(std::move(exception));
    return RecurrenceSeries{id_,
                            tenant_,
                            tutor_,
                            participants_,
                            rule_,
                            starts_on_,
                            at_,
                            zone_,
                            duration_,
                            std::move(grown)};
}

core::Result<std::pair<RecurrenceSeries, RecurrenceSeries>> RecurrenceSeries::SplitAt(
    core::Date from, core::SeriesId next, RecurrenceRule rule) const {
    if (from <= starts_on_) {
        return core::Error{core::ErrorKind::kValidation,
                           "recurrence_split_before_start",
                           "разрез раньше начала серии не оставляет от неё ничего: заведите "
                           "новую серию вместо разреза"};
    }

    const auto ended =
        RecurrenceRule::Compose(rule_.Interval(), rule_.Days(), Ending{Until{DayBefore(from)}});
    if (!ended.HasValue()) {
        return ended.Failure();
    }

    std::vector<RecurrenceException> past;
    std::vector<RecurrenceException> future;
    for (const auto& exception : exceptions_) {
        (exception.occurrence_on < from ? past : future).push_back(exception);
    }

    RecurrenceSeries before{id_,
                            tenant_,
                            tutor_,
                            participants_,
                            ended.Value(),
                            starts_on_,
                            at_,
                            zone_,
                            duration_,
                            std::move(past)};
    RecurrenceSeries after{std::move(next),
                           tenant_,
                           tutor_,
                           participants_,
                           std::move(rule),
                           from,
                           at_,
                           zone_,
                           duration_,
                           std::move(future)};

    return std::pair<RecurrenceSeries, RecurrenceSeries>{std::move(before), std::move(after)};
}

core::Result<std::vector<Occurrence>> Expand(const RecurrenceSeries& series,
                                             const core::TimeRange& window,
                                             const core::ZoneOffsets& offsets,
                                             core::Instant::Duration horizon) {
    if (horizon <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "expansion_horizon_not_positive",
                           "горизонт развёртки нулевой длины не даёт ни одного занятия"};
    }
    if (window.Length() > horizon) {
        return core::Error{core::ErrorKind::kValidation,
                           "expansion_window_over_horizon",
                           "запрошенный отрезок длиннее горизонта развёртки: расписание считается "
                           "по частям, а не всё сразу"};
    }

    const auto& rule = series.Rule();
    const auto* count = std::get_if<Count>(&rule.Ends());
    const auto* until = std::get_if<Until>(&rule.Ends());
    const auto week_start = WeekStartOf(series.StartsOn());

    std::vector<Occurrence> found;
    int produced = 0;

    for (int week = 0;; week += rule.Interval()) {
        bool exhausted = true;

        for (const auto day : rule.Days()) {
            const auto date = DateOf(week_start, day, week);
            if (date < series.StartsOn()) {
                exhausted = false;
                continue;
            }
            if (until != nullptr && until->date < date) {
                continue;
            }
            if (count != nullptr && produced >= count->times) {
                continue;
            }

            exhausted = false;
            ++produced;

            const auto* exception = ExceptionOn(series.Exceptions(), date);
            if (exception != nullptr && exception->kind == ExceptionKind::kCancelled) {
                continue;
            }

            Occurrence occurrence{
                date, core::Instant::FromUnixMicros(0), series.LessonDuration(), Placement::kExact};

            if (exception != nullptr && exception->kind == ExceptionKind::kMoved) {
                occurrence.starts_at = *exception->moved_to;
                occurrence.placement = Placement::kMovedByHand;
                if (exception->moved_duration.has_value()) {
                    occurrence.duration = *exception->moved_duration;
                }
            } else {
                const auto resolved =
                    core::Resolve(core::LocalDateTime{date, series.At()}, offsets);
                occurrence.starts_at = resolved.first;
                if (resolved.kind == core::ResolveResult::Kind::kSkipped) {
                    occurrence.placement = Placement::kMissingAfterClockChange;
                } else if (resolved.kind == core::ResolveResult::Kind::kAmbiguous) {
                    occurrence.placement = Placement::kTwiceOnTheClock;
                }
            }

            if (window.Contains(occurrence.starts_at)) {
                found.push_back(occurrence);
            }
        }

        if (exhausted) {
            break;
        }
        if (produced > kMaxOccurrences) {
            return core::Error{core::ErrorKind::kValidation,
                               "recurrence_series_too_long",
                               "в серии больше занятий, чем расписание берётся считать: "
                               "разбейте её на несколько"};
        }
    }

    return found;
}

}  // namespace pdr::scheduling
