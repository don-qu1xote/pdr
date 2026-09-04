#include "core/types/local_time.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "builders/moment_builder.hpp"

namespace pdr::core {
namespace {

using namespace std::chrono_literals;
using pdr::testing::MomentBuilder;

Date On(int year, unsigned month, unsigned day) {
    return Date::Compose(year, month, day).Value();
}

LocalDateTime At(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    return LocalDateTime{On(year, month, day), LocalTime::Compose(hour, minute).Value()};
}

Instant Utc(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, minute).Build();
}

/// ЕВРОПА/БЕРЛИН 2026: последнее воскресенье марта и последнее воскресенье
/// октября. Весной 01:00 UTC часы идут с +1 на +2, осенью 01:00 UTC — обратно.
///
/// Таблица написана руками и намеренно: базы IANA у ядра нет, и её отсутствие —
/// не помеха проверить именно ту арифметику, ради которой всё затевалось.
ZoneOffsets Berlin() {
    return ZoneOffsets::Compose(
               1h,
               {OffsetShift{Utc(2026, 3, 29, 1, 0), 2h}, OffsetShift{Utc(2026, 10, 25, 1, 0), 1h}})
        .Value();
}

/// Европа/Москва после 2014-го: переводов нет вовсе.
ZoneOffsets Moscow() {
    return ZoneOffsets::Fixed(3h);
}

}  // namespace

TEST(Resolve, AnOrdinaryEveningIsOneMoment) {
    const auto resolved = Resolve(At(2026, 3, 2, 18, 30), Berlin());

    EXPECT_EQ(resolved.kind, ResolveResult::Kind::kUnique);
    EXPECT_EQ(resolved.first, Utc(2026, 3, 2, 17, 30));
    EXPECT_FALSE(resolved.second.has_value());
}

/// ВЕСНА: ЧАСА НЕ БЫЛО ВОВСЕ.
///
/// В ночь на 29 марта 2026-го часы в Берлине идут с 02:00 сразу на 03:00.
/// «02:30» в этот день не существует, и функция, возвращающая просто Instant,
/// обязана здесь соврать.
TEST(Resolve, TheHourThatSpringTookAwayHasNoMoment) {
    const auto resolved = Resolve(At(2026, 3, 29, 2, 30), Berlin());

    EXPECT_EQ(resolved.kind, ResolveResult::Kind::kSkipped);
    EXPECT_FALSE(resolved.second.has_value());
    EXPECT_EQ(resolved.first, Utc(2026, 3, 29, 1, 0));
}

TEST(Resolve, TheEdgesOfTheSpringGapBehaveLikeTheGap) {
    EXPECT_EQ(Resolve(At(2026, 3, 29, 2, 0), Berlin()).kind, ResolveResult::Kind::kSkipped);
    EXPECT_EQ(Resolve(At(2026, 3, 29, 2, 59), Berlin()).kind, ResolveResult::Kind::kSkipped);

    const auto after = Resolve(At(2026, 3, 29, 3, 0), Berlin());
    EXPECT_EQ(after.kind, ResolveResult::Kind::kUnique);
    EXPECT_EQ(after.first, Utc(2026, 3, 29, 1, 0));

    const auto before = Resolve(At(2026, 3, 29, 1, 59), Berlin());
    EXPECT_EQ(before.kind, ResolveResult::Kind::kUnique);
    EXPECT_EQ(before.first, Utc(2026, 3, 29, 0, 59));
}

/// ОСЕНЬ: ЧАС СЛУЧИЛСЯ ДВАЖДЫ.
///
/// В ночь на 25 октября 2026-го часы идут с 03:00 назад на 02:00. «02:30»
/// в этот день бывает дважды, и разница между ними — целый час.
TEST(Resolve, TheHourThatAutumnRepeatedHasTwoMoments) {
    const auto resolved = Resolve(At(2026, 10, 25, 2, 30), Berlin());

    ASSERT_EQ(resolved.kind, ResolveResult::Kind::kAmbiguous);
    ASSERT_TRUE(resolved.second.has_value());
    EXPECT_EQ(resolved.first, Utc(2026, 10, 25, 0, 30));
    EXPECT_EQ(*resolved.second, Utc(2026, 10, 25, 1, 30));
    EXPECT_EQ(*resolved.second - resolved.first, std::chrono::duration_cast<Instant::Duration>(1h));
}

TEST(Resolve, TheEdgesOfTheAutumnOverlapBehaveLikeTheOverlap) {
    EXPECT_EQ(Resolve(At(2026, 10, 25, 2, 0), Berlin()).kind, ResolveResult::Kind::kAmbiguous);
    EXPECT_EQ(Resolve(At(2026, 10, 25, 2, 59), Berlin()).kind, ResolveResult::Kind::kAmbiguous);
    EXPECT_EQ(Resolve(At(2026, 10, 25, 3, 0), Berlin()).kind, ResolveResult::Kind::kUnique);
    EXPECT_EQ(Resolve(At(2026, 10, 25, 1, 59), Berlin()).kind, ResolveResult::Kind::kUnique);
}

TEST(Resolve, AZoneWithoutShiftsNeverSurprises) {
    for (const auto hour : {0U, 2U, 3U, 12U, 23U}) {
        const auto spring = Resolve(At(2026, 3, 29, hour, 30), Moscow());
        const auto autumn = Resolve(At(2026, 10, 25, hour, 30), Moscow());

        EXPECT_EQ(spring.kind, ResolveResult::Kind::kUnique);
        EXPECT_EQ(autumn.kind, ResolveResult::Kind::kUnique);
    }
}

/// РЕПЕТИТОР И УЧЕНИК В РАЗНЫХ ЗОНАХ: момент один, часы разные.
///
/// Это и есть весь смысл хранить момент, а не местное время: занятие ОДНО, и
/// каждый видит его по своим часам, а не по чужим.
TEST(ToLocal, OneMomentLooksRightOnBothClocks) {
    const auto lesson = Resolve(At(2026, 3, 2, 18, 0), Moscow());
    ASSERT_EQ(lesson.kind, ResolveResult::Kind::kUnique);
    EXPECT_EQ(lesson.first, Utc(2026, 3, 2, 15, 0));

    const auto tutor = ToLocal(lesson.first, Moscow());
    EXPECT_EQ(tutor.AtTime(), LocalTime::Compose(18, 0).Value());
    EXPECT_EQ(tutor.OnDate(), On(2026, 3, 2));

    const auto student = ToLocal(lesson.first, Berlin());
    EXPECT_EQ(student.AtTime(), LocalTime::Compose(16, 0).Value());
    EXPECT_EQ(student.OnDate(), On(2026, 3, 2));
}

/// Та же пара после весеннего перевода: разница между зонами СТАЛА ДРУГОЙ.
/// Занятие «в 18:00 по Москве» у берлинского ученика сдвинулось на час — и это
/// не ошибка, а то, ради чего момент хранится в UTC.
TEST(ToLocal, TheGapBetweenTheClocksChangesWithTheSeason) {
    const auto winter = Resolve(At(2026, 3, 2, 18, 0), Moscow()).first;
    const auto summer = Resolve(At(2026, 6, 2, 18, 0), Moscow()).first;

    EXPECT_EQ(ToLocal(winter, Berlin()).AtTime(), LocalTime::Compose(16, 0).Value());
    EXPECT_EQ(ToLocal(summer, Berlin()).AtTime(), LocalTime::Compose(17, 0).Value());
}

TEST(ToLocal, ThePreviousDayIsStillTheRightDay) {
    const auto late = Utc(2026, 3, 2, 22, 30);

    const auto moscow = ToLocal(late, Moscow());
    EXPECT_EQ(moscow.OnDate(), On(2026, 3, 3));
    EXPECT_EQ(moscow.AtTime(), LocalTime::Compose(1, 30).Value());
}

TEST(ToLocal, ResolveAndToLocalAgreeOnEveryOrdinaryMoment) {
    for (const unsigned hour : {0U, 6U, 12U, 18U, 23U}) {
        const auto local = At(2026, 6, 15, hour, 45);
        const auto resolved = Resolve(local, Berlin());

        ASSERT_EQ(resolved.kind, ResolveResult::Kind::kUnique) << "час " << hour;
        EXPECT_EQ(ToLocal(resolved.first, Berlin()), local) << "час " << hour;
    }
}

TEST(Date, RefusesWhatTheCalendarDoesNotHave) {
    EXPECT_FALSE(Date::Compose(2026, 2, 30).HasValue());
    EXPECT_FALSE(Date::Compose(2026, 13, 1).HasValue());
    EXPECT_FALSE(Date::Compose(1800, 1, 1).HasValue());
    EXPECT_TRUE(Date::Compose(2028, 2, 29).HasValue());
}

TEST(Date, KnowsItsDayOfWeek) {
    EXPECT_EQ(On(2026, 3, 2).DayOfWeek(), Weekday::kMonday);
    EXPECT_EQ(On(2026, 3, 29).DayOfWeek(), Weekday::kSunday);
}

TEST(LocalTime, RefusesWhatIsNotOnTheClock) {
    EXPECT_FALSE(LocalTime::Compose(24, 0).HasValue());
    EXPECT_FALSE(LocalTime::Compose(0, 60).HasValue());
    EXPECT_TRUE(LocalTime::Compose(23, 59).HasValue());
}

TEST(TimeRange, RefusesToGoBackwardsOrNowhere) {
    const auto from = Utc(2026, 3, 2, 18, 0);
    const auto to = Utc(2026, 3, 2, 19, 0);

    EXPECT_TRUE(TimeRange::Compose(from, to).HasValue());
    EXPECT_FALSE(TimeRange::Compose(to, from).HasValue());
    EXPECT_FALSE(TimeRange::Compose(from, from).HasValue());
}

TEST(TimeRange, TouchingEndsAreNotAnIntersection) {
    const auto first = TimeRange::Compose(Utc(2026, 3, 2, 18, 0), Utc(2026, 3, 2, 19, 0)).Value();
    const auto next = TimeRange::Compose(Utc(2026, 3, 2, 19, 0), Utc(2026, 3, 2, 20, 0)).Value();
    const auto overlapping =
        TimeRange::Compose(Utc(2026, 3, 2, 18, 59), Utc(2026, 3, 2, 20, 0)).Value();

    EXPECT_FALSE(first.Intersects(next));
    EXPECT_FALSE(next.Intersects(first));
    EXPECT_TRUE(first.Intersects(overlapping));
}

TEST(ZoneOffsets, RefusesShiftsThatDoNotDescribeAShift) {
    EXPECT_FALSE(ZoneOffsets::Compose(1h, {OffsetShift{Utc(2026, 3, 29, 1, 0), 1h}}).HasValue());
    EXPECT_FALSE(
        ZoneOffsets::Compose(
            1h, {OffsetShift{Utc(2026, 10, 25, 1, 0), 2h}, OffsetShift{Utc(2026, 3, 29, 1, 0), 1h}})
            .HasValue());
}

}  // namespace pdr::core
