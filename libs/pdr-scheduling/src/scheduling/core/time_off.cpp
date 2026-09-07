#include "scheduling/core/time_off.hpp"

#include <chrono>

namespace pdr::scheduling {
namespace {

std::chrono::sys_days AsDays(const core::Date& date) {
    return std::chrono::sys_days{std::chrono::year_month_day{
        std::chrono::year{date.Year()},
        std::chrono::month{date.Month()},
        std::chrono::day{date.Day()},
    }};
}

core::Date DayAfter(const core::Date& date) {
    const std::chrono::year_month_day next{AsDays(date) + std::chrono::days{1}};
    return core::Date::Compose(static_cast<int>(next.year()),
                               static_cast<unsigned>(next.month()),
                               static_cast<unsigned>(next.day()))
        .Value();
}

}  // namespace

std::string_view Name(SeriesDecision decision) noexcept {
    switch (decision) {
        case SeriesDecision::kSkip:
            return "skip";
        case SeriesDecision::kShift:
            return "shift";
        case SeriesDecision::kBoundary:
            break;
    }
    return "skip";
}

std::optional<SeriesDecision> ParseSeriesDecision(std::string_view text) {
    for (const auto decision : {SeriesDecision::kSkip, SeriesDecision::kShift}) {
        if (Name(decision) == text) {
            return decision;
        }
    }
    return std::nullopt;
}

std::optional<TimeOffReason> ParseTimeOffReason(std::string_view text) {
    for (const auto reason :
         {TimeOffReason::kVacation, TimeOffReason::kSickness, TimeOffReason::kBreak}) {
        if (Name(reason) == text) {
            return reason;
        }
    }
    return std::nullopt;
}

std::optional<TimeOffDecision> ParseTimeOffDecision(std::string_view text) {
    for (const auto decision :
         {TimeOffDecision::kCancel, TimeOffDecision::kPostpone, TimeOffDecision::kKeep}) {
        if (Name(decision) == text) {
            return decision;
        }
    }
    return std::nullopt;
}

core::Result<TimeOff> TimeOff::Compose(core::TimeOffId id,
                                       core::TenantId tenant,
                                       core::PersonId person,
                                       core::Date from,
                                       core::Date to,
                                       std::optional<TimeOffReason> reason,
                                       core::TimeZone zone,
                                       core::PersonId declared_by) {
    /// Проверки «не в прошлом» здесь нет и быть не может: `now` не передан.
    /// Заводят перерыв обычно уже во время него.
    if (to < from) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_off_ends_before_it_starts",
                           "перерыв кончается раньше, чем начинается: проверьте даты"};
    }
    if (reason.has_value() && *reason == TimeOffReason::kBoundary) {
        return core::Error{
            core::ErrorKind::kValidation, "time_off_reason_unknown", "такой причины не бывает"};
    }

    const auto days = (AsDays(to) - AsDays(from)).count() + 1;
    if (days > kMaxDays) {
        return core::Error{core::ErrorKind::kValidation,
                           "time_off_too_long",
                           "перерыв длиннее года — обычно это описка в дате"};
    }

    return TimeOff{std::move(id),
                   std::move(tenant),
                   std::move(person),
                   from,
                   to,
                   reason,
                   std::move(zone),
                   std::move(declared_by)};
}

core::Date TimeOff::ResumesOn() const {
    return DayAfter(to_);
}

int TimeOff::Days() const noexcept {
    return static_cast<int>((AsDays(to_) - AsDays(from_)).count()) + 1;
}

bool TimeOff::Covers(const core::Date& date) const noexcept {
    return date >= from_ && date <= to_;
}

int WeeksOver(const TimeOff& period) noexcept {
    /// Дней в перерыве считая оба конца — от одного до семи это одна неделя, от
    /// восьми до четырнадцати две. Целочисленное деление с округлением вверх.
    return (period.Days() + 6) / 7;
}

}  // namespace pdr::scheduling
