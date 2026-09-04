#include "core/types/local_time.hpp"

#include <algorithm>
#include <utility>

namespace pdr::core {
namespace {

constexpr int kEarliestYear = 1900;
constexpr int kLatestYear = 2200;

std::chrono::year_month_day AsCalendar(const Date& date) noexcept {
    return std::chrono::year_month_day{std::chrono::year{date.Year()},
                                       std::chrono::month{date.Month()},
                                       std::chrono::day{date.Day()}};
}

Date FromCalendar(const std::chrono::year_month_day& date) noexcept {
    return Date::Compose(static_cast<int>(date.year()),
                         static_cast<unsigned>(date.month()),
                         static_cast<unsigned>(date.day()))
        .Value();
}

}  // namespace

std::string_view Name(Weekday day) noexcept {
    switch (day) {
        case Weekday::kSunday:
            return "sunday";
        case Weekday::kMonday:
            return "monday";
        case Weekday::kTuesday:
            return "tuesday";
        case Weekday::kWednesday:
            return "wednesday";
        case Weekday::kThursday:
            return "thursday";
        case Weekday::kFriday:
            return "friday";
        case Weekday::kSaturday:
            return "saturday";
        case Weekday::kBoundary:
            break;
    }
    return "sunday";
}

Result<Date> Date::Compose(int year, unsigned month, unsigned day) {
    if (year < kEarliestYear || year > kLatestYear) {
        return Error{ErrorKind::kValidation,
                     "date_year_out_of_range",
                     "год вне диапазона, в котором календарь имеет смысл"};
    }

    const std::chrono::year_month_day date{
        std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!date.ok()) {
        return Error{ErrorKind::kValidation, "date_does_not_exist", "такой даты не существует"};
    }

    return Date{year, month, day};
}

Weekday Date::DayOfWeek() const noexcept {
    const std::chrono::weekday day{std::chrono::sys_days{AsCalendar(*this)}};
    return static_cast<Weekday>(day.c_encoding());
}

Result<LocalTime> LocalTime::Compose(unsigned hour, unsigned minute) {
    if (hour > 23 || minute > 59) {
        return Error{ErrorKind::kValidation,
                     "local_time_off_the_clock",
                     "такого времени на часах не бывает"};
    }
    return LocalTime{hour, minute};
}

Instant::Duration LocalDateTime::AsIfUtc() const noexcept {
    const auto days = std::chrono::sys_days{AsCalendar(date_)}.time_since_epoch();
    return std::chrono::duration_cast<Instant::Duration>(days + time_.SinceMidnight());
}

Result<TimeRange> TimeRange::Compose(Instant from, Instant to) {
    if (to <= from) {
        return Error{ErrorKind::kValidation,
                     "time_range_not_forward",
                     "отрезок кончается не позже, чем начался"};
    }
    return TimeRange{from, to};
}

Result<ZoneOffsets> ZoneOffsets::Compose(Instant::Duration initial,
                                         std::vector<OffsetShift> shifts) {
    auto previous = initial;
    for (std::size_t index = 0; index < shifts.size(); ++index) {
        if (index > 0 && shifts[index].at <= shifts[index - 1].at) {
            return Error{ErrorKind::kValidation,
                         "zone_shifts_out_of_order",
                         "переводы часов идут не по возрастанию момента"};
        }
        if (shifts[index].offset == previous) {
            return Error{ErrorKind::kValidation,
                         "zone_shift_changes_nothing",
                         "перевод, который ничего не меняет, описывает перевод, которого не было"};
        }
        previous = shifts[index].offset;
    }

    return ZoneOffsets{initial, std::move(shifts)};
}

ZoneOffsets ZoneOffsets::Fixed(Instant::Duration offset) {
    return ZoneOffsets{offset, {}};
}

Instant::Duration ZoneOffsets::At(Instant moment) const noexcept {
    auto offset = initial_;
    for (const auto& shift : shifts_) {
        if (shift.at > moment) {
            break;
        }
        offset = shift.offset;
    }
    return offset;
}

std::string_view Name(ResolveResult::Kind kind) noexcept {
    switch (kind) {
        case ResolveResult::Kind::kUnique:
            return "unique";
        case ResolveResult::Kind::kSkipped:
            return "skipped";
        case ResolveResult::Kind::kAmbiguous:
            return "ambiguous";
    }
    return "unique";
}

/// РАЗБОР ПО ВСЕМ СМЕЩЕНИЯМ ЗОНЫ, А НЕ ПО СОСЕДНИМ.
///
/// Для каждого смещения, которое зона когда-либо имела, считается момент
/// `local - offset` и ПРОВЕРЯЕТСЯ, действовало ли в этот момент именно оно.
/// Прошедших проверку нуль — час пропал, один — обычный случай, два — час
/// повторился. Третьего исхода у календаря не бывает: смещения соседних
/// сегментов различаются, и совпасть могут ровно два.
///
/// Смещений у зоны единицы, поэтому перебор дешевле любой хитрости — и, в
/// отличие от неё, очевидно верен.
ResolveResult Resolve(const LocalDateTime& local, const ZoneOffsets& offsets) {
    const auto wall = local.AsIfUtc();

    std::vector<Instant::Duration> candidates{offsets.Initial()};
    for (const auto& shift : offsets.Shifts()) {
        candidates.push_back(shift.offset);
    }
    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    std::vector<Instant> found;
    for (const auto offset : candidates) {
        const auto moment = Instant::FromUnixMicros((wall - offset).count());
        if (offsets.At(moment) == offset) {
            found.push_back(moment);
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());

    if (found.size() == 1) {
        return ResolveResult{ResolveResult::Kind::kUnique, found.front(), std::nullopt};
    }
    if (found.size() > 1) {
        return ResolveResult{ResolveResult::Kind::kAmbiguous, found.front(), found.back()};
    }

    for (const auto& shift : offsets.Shifts()) {
        const auto before = offsets.At(shift.at - Instant::Duration{1});
        if (shift.offset <= before) {
            continue;
        }
        const auto gap_from = (shift.at - Instant::FromUnixMicros(0)) + before;
        const auto gap_to = (shift.at - Instant::FromUnixMicros(0)) + shift.offset;
        if (wall >= gap_from && wall < gap_to) {
            return ResolveResult{ResolveResult::Kind::kSkipped, shift.at, std::nullopt};
        }
    }

    return ResolveResult{
        ResolveResult::Kind::kSkipped, Instant::FromUnixMicros(wall.count()), std::nullopt};
}

LocalDateTime ToLocal(Instant moment, const ZoneOffsets& offsets) {
    const auto wall = (moment - Instant::FromUnixMicros(0)) + offsets.At(moment);
    const auto days = std::chrono::floor<std::chrono::days>(wall);
    const auto since_midnight = std::chrono::duration_cast<std::chrono::minutes>(wall - days);

    const std::chrono::year_month_day date{std::chrono::sys_days{days}};
    const auto hours = std::chrono::duration_cast<std::chrono::hours>(since_midnight);

    return LocalDateTime{FromCalendar(date),
                         LocalTime::Compose(static_cast<unsigned>(hours.count()),
                                            static_cast<unsigned>((since_midnight - hours).count()))
                             .Value()};
}

}  // namespace pdr::core
