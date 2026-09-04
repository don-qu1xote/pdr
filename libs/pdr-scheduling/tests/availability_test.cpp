#include "scheduling/core/availability.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "builders/moment_builder.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::testing::MomentBuilder;

core::TimeZone Zone(std::string_view name) {
    return core::TimeZone::Parse(name).value();
}

core::LocalTime Clock(unsigned hour, unsigned minute) {
    return core::LocalTime::Compose(hour, minute).Value();
}

core::Date On(int year, unsigned month, unsigned day) {
    return core::Date::Compose(year, month, day).Value();
}

core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, minute).Build();
}

core::TimeRange Span(core::Instant from, core::Instant::Duration length) {
    return core::TimeRange::Compose(from, from + length).Value();
}

/// Берлин 2026: переводы в последнее воскресенье марта и октября.
core::ZoneOffsets Berlin() {
    return core::ZoneOffsets::Compose(1h,
                                      {core::OffsetShift{Utc(2026, 3, 29, 1, 0), 2h},
                                       core::OffsetShift{Utc(2026, 10, 25, 1, 0), 1h}})
        .Value();
}

core::ZoneOffsets Moscow() {
    return core::ZoneOffsets::Fixed(3h);
}

AvailabilityRule Weekly(core::Weekday day, unsigned from, unsigned to, std::string_view zone) {
    return AvailabilityRule::Compose(day, Clock(from, 0), Clock(to, 0), Zone(zone)).Value();
}

Availability Weekdays(std::vector<AvailabilityException> exceptions = {}) {
    return Availability::Compose({Weekly(core::Weekday::kMonday, 10, 18, "Europe/Moscow"),
                                  Weekly(core::Weekday::kTuesday, 10, 18, "Europe/Moscow")},
                                 std::move(exceptions))
        .Value();
}

const auto kHour = std::chrono::duration_cast<core::Instant::Duration>(1h);

}  // namespace

TEST(Availability, ALessonInsideTheWindowIsInside) {
    const auto lesson = Span(Utc(2026, 3, 2, 12, 0), kHour);

    EXPECT_EQ(Weekdays().Covers(lesson, Moscow()), AvailabilityVerdict::kInside);
}

TEST(Availability, ALessonTouchingTheEdgesIsStillInside) {
    EXPECT_EQ(Weekdays().Covers(Span(Utc(2026, 3, 2, 7, 0), kHour), Moscow()),
              AvailabilityVerdict::kInside);
    EXPECT_EQ(Weekdays().Covers(Span(Utc(2026, 3, 2, 14, 0), kHour), Moscow()),
              AvailabilityVerdict::kInside);
}

/// ЗАНЯТИЕ ВНЕ ДОСТУПНОСТИ НЕ ЗАПРЕЩЕНО, А ОТМЕЧЕНО.
///
/// Продукт не мешает человеку работать по-своему: воскресное занятие
/// возможно — но записать его молча нельзя.
TEST(Availability, ALessonOutsideTheWindowAsksForConfirmation) {
    EXPECT_EQ(Weekdays().Covers(Span(Utc(2026, 3, 1, 12, 0), kHour), Moscow()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
    EXPECT_EQ(Weekdays().Covers(Span(Utc(2026, 3, 2, 20, 0), kHour), Moscow()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
}

/// Смотрится ВЕСЬ отрезок: занятие, начавшееся в последний доступный час и
/// идущее два, выходит за доступность своим хвостом.
TEST(Availability, ALessonRunningPastTheEdgeAsksForConfirmation) {
    const auto tail = Span(Utc(2026, 3, 2, 14, 0), 2 * kHour);

    EXPECT_EQ(Weekdays().Covers(tail, Moscow()), AvailabilityVerdict::kOutsideNeedsConfirmation);
}

TEST(Availability, AnExceptionWithoutHoursIsADayOff) {
    const auto off = Weekdays({AvailabilityException{On(2026, 3, 2), std::nullopt}});

    EXPECT_EQ(off.Covers(Span(Utc(2026, 3, 2, 12, 0), kHour), Moscow()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
}

TEST(Availability, AnExceptionWithHoursReplacesTheRuleEntirely) {
    const auto shifted =
        Weekdays({AvailabilityException{On(2026, 3, 2), Span(Utc(2026, 3, 2, 17, 0), 2 * kHour)}});

    EXPECT_EQ(shifted.Covers(Span(Utc(2026, 3, 2, 17, 30), kHour), Moscow()),
              AvailabilityVerdict::kInside);
    EXPECT_EQ(shifted.Covers(Span(Utc(2026, 3, 2, 12, 0), kHour), Moscow()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
}

TEST(Availability, RefusesTwoExceptionsOnOneDate) {
    const auto twice = Availability::Compose({},
                                             {AvailabilityException{On(2026, 3, 2), std::nullopt},
                                              AvailabilityException{On(2026, 3, 2), std::nullopt}});

    ASSERT_FALSE(twice.HasValue());
    EXPECT_EQ(twice.Failure().Code(), "availability_exception_repeated");
}

/// ДОСТУПНОСТЬ СЧИТАЕТСЯ ПО ЧАСАМ РЕПЕТИТОРА, А НЕ ПО UTC.
///
/// «По понедельникам с десяти до восемнадцати» у берлинского репетитора и у
/// московского — это разные отрезки в UTC, и занятие, попадающее одному, у
/// другого выходит за край.
TEST(Availability, TheWindowFollowsTheTutorsOwnClock) {
    const auto berlin =
        Availability::Compose({Weekly(core::Weekday::kMonday, 10, 18, "Europe/Berlin")}, {})
            .Value();
    const auto lesson = Span(Utc(2026, 3, 2, 15, 30), kHour);

    EXPECT_EQ(berlin.Covers(lesson, Berlin()), AvailabilityVerdict::kInside);
    EXPECT_EQ(Weekdays().Covers(lesson, Moscow()), AvailabilityVerdict::kOutsideNeedsConfirmation);
}

/// ПОСЛЕ ПЕРЕВОДА ЧАСОВ ОКНО ЕДЕТ В UTC, А НА ЧАСАХ РЕПЕТИТОРА ОСТАЁТСЯ ТЕМ ЖЕ.
///
/// Один и тот же момент по UTC — 08:30 в понедельник — зимой у берлинского
/// репетитора приходится на 09:30 и в окно не попадает, а летом на 10:30 и
/// попадает. Это и есть причина хранить зону вместе с правилом: «с десяти»
/// значит одно и то же для человека и разное для UTC.
TEST(Availability, TheWindowMovesInUtcWhenTheClocksChange) {
    const auto berlin =
        Availability::Compose({Weekly(core::Weekday::kMonday, 10, 18, "Europe/Berlin")}, {})
            .Value();

    EXPECT_EQ(berlin.Covers(Span(Utc(2026, 3, 2, 8, 30), kHour), Berlin()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
    EXPECT_EQ(berlin.Covers(Span(Utc(2026, 6, 1, 8, 30), kHour), Berlin()),
              AvailabilityVerdict::kInside);
}

/// НОЧЬ ПЕРЕВОДА: правило, попавшее на пропавший час, в этот день не действует.
///
/// Доступность «с двух до трёх» у берлинского репетитора в ночь на 29 марта
/// приходится ровно на час, которого не было. Это не поломка настройки — это
/// календарь, и занятие в такую ночь просто требует подтверждения.
TEST(Availability, ARuleThatFallsIntoTheSpringGapDoesNotHoldThatDay) {
    const auto nightly =
        Availability::Compose({Weekly(core::Weekday::kSunday, 2, 3, "Europe/Berlin")}, {}).Value();

    EXPECT_EQ(nightly.Covers(Span(Utc(2026, 3, 29, 1, 0), 30min), Berlin()),
              AvailabilityVerdict::kOutsideNeedsConfirmation);
    EXPECT_EQ(nightly.Covers(Span(Utc(2026, 3, 22, 1, 0), 30min), Berlin()),
              AvailabilityVerdict::kInside);
}

TEST(AvailabilityRule, RefusesAWindowThatGoesNowhere) {
    EXPECT_FALSE(AvailabilityRule::Compose(
                     core::Weekday::kMonday, Clock(18, 0), Clock(10, 0), Zone("Europe/Moscow"))
                     .HasValue());
    EXPECT_FALSE(AvailabilityRule::Compose(
                     core::Weekday::kMonday, Clock(10, 0), Clock(10, 0), Zone("Europe/Moscow"))
                     .HasValue());
    EXPECT_FALSE(AvailabilityRule::Compose(
                     core::Weekday::kBoundary, Clock(10, 0), Clock(18, 0), Zone("Europe/Moscow"))
                     .HasValue());
}

}  // namespace pdr::scheduling
