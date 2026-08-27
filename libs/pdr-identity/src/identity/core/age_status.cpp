#include "identity/core/age_status.hpp"

#include <chrono>

namespace pdr::identity {
namespace {

std::chrono::year_month_day CivilDayOf(core::Instant moment) {
    const std::chrono::sys_time<std::chrono::microseconds> point{
        std::chrono::microseconds{moment.UnixMicros()}};
    return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(point)};
}

}  // namespace

core::Instant AgeStatus::TurnsAt(const BirthDate& born_on, int years) noexcept {
    const auto born = born_on.Value();
    auto birthday = std::chrono::year_month_day{
        born.year() + std::chrono::years{years}, born.month(), born.day()};
    if (!birthday.ok()) {
        birthday = std::chrono::year_month_day{
            std::chrono::sys_days{birthday.year() / birthday.month() / std::chrono::last} +
            std::chrono::days{1}};
    }

    const auto midnight = std::chrono::sys_days{birthday};
    return core::Instant::FromUnixMicros(
        std::chrono::duration_cast<std::chrono::microseconds>(midnight.time_since_epoch()).count());
}

core::Result<AgeStatus> AgeStatus::At(BirthDate born_on, core::Instant moment) {
    const auto today = CivilDayOf(moment);
    const auto born = born_on.Value();

    int years = static_cast<int>(today.year()) - static_cast<int>(born.year());
    if (today.month() / today.day() < born.month() / born.day()) {
        --years;
    }

    if (years < 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "age_before_birth",
                           "дата рождения позже сегодняшнего дня"};
    }

    return AgeStatus{born_on, moment, years};
}

}  // namespace pdr::identity
