#include "scheduling/core/availability.hpp"

#include <algorithm>
#include <utility>

namespace pdr::scheduling {
namespace {

/// Отрезок доступности правила на конкретную местную дату — в моментах.
///
/// Пусто, когда местное время этого дня не существует: в ночь весеннего
/// перевода доступность «с двух до трёх» приходится ровно на пропавший час.
/// Это не ошибка настройки, а свойство календаря, и правило в такой день просто
/// не действует.
std::optional<core::TimeRange> RuleSpanOn(const AvailabilityRule& rule,
                                          const core::Date& date,
                                          const core::ZoneOffsets& offsets) {
    const auto from = core::Resolve(core::LocalDateTime{date, rule.From()}, offsets);
    const auto to = core::Resolve(core::LocalDateTime{date, rule.To()}, offsets);
    if (from.kind == core::ResolveResult::Kind::kSkipped ||
        to.kind == core::ResolveResult::Kind::kSkipped) {
        return std::nullopt;
    }

    const auto span = core::TimeRange::Compose(from.first, to.first);
    if (!span.HasValue()) {
        return std::nullopt;
    }
    return span.Value();
}

bool Inside(const core::TimeRange& lesson, const core::TimeRange& window) noexcept {
    return lesson.From() >= window.From() && lesson.To() <= window.To();
}

}  // namespace

std::string_view Name(AvailabilityVerdict verdict) noexcept {
    switch (verdict) {
        case AvailabilityVerdict::kInside:
            return "inside";
        case AvailabilityVerdict::kOutsideNeedsConfirmation:
            return "outside_needs_confirmation";
    }
    return "inside";
}

core::Result<AvailabilityRule> AvailabilityRule::Compose(core::Weekday day,
                                                         core::LocalTime from,
                                                         core::LocalTime to,
                                                         core::TimeZone zone) {
    if (day == core::Weekday::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "availability_day_out_of_week",
                           "такого дня недели не бывает"};
    }
    if (to <= from) {
        return core::Error{core::ErrorKind::kValidation,
                           "availability_window_not_forward",
                           "доступность кончается не позже, чем началась"};
    }
    return AvailabilityRule{day, from, to, std::move(zone)};
}

core::Result<Availability> Availability::Compose(std::vector<AvailabilityRule> rules,
                                                 std::vector<AvailabilityException> exceptions) {
    auto dates = exceptions;
    std::sort(dates.begin(), dates.end(), [](const auto& left, const auto& right) {
        return left.date < right.date;
    });
    const auto repeated =
        std::adjacent_find(dates.begin(), dates.end(), [](const auto& left, const auto& right) {
            return left.date == right.date;
        });
    if (repeated != dates.end()) {
        return core::Error{core::ErrorKind::kValidation,
                           "availability_exception_repeated",
                           "на одну дату заведено два исключения: какое из них сильнее, "
                           "не сказано"};
    }

    return Availability{std::move(rules), std::move(exceptions)};
}

AvailabilityVerdict Availability::Covers(const core::TimeRange& lesson,
                                         const core::ZoneOffsets& offsets) const {
    const auto local = core::ToLocal(lesson.From(), offsets);
    const auto& date = local.OnDate();

    for (const auto& exception : exceptions_) {
        if (exception.date != date) {
            continue;
        }
        if (!exception.instead.has_value()) {
            return AvailabilityVerdict::kOutsideNeedsConfirmation;
        }
        return Inside(lesson, *exception.instead) ? AvailabilityVerdict::kInside
                                                  : AvailabilityVerdict::kOutsideNeedsConfirmation;
    }

    for (const auto& rule : rules_) {
        if (rule.Day() != date.DayOfWeek()) {
            continue;
        }
        const auto window = RuleSpanOn(rule, date, offsets);
        if (window.has_value() && Inside(lesson, *window)) {
            return AvailabilityVerdict::kInside;
        }
    }

    return AvailabilityVerdict::kOutsideNeedsConfirmation;
}

}  // namespace pdr::scheduling
