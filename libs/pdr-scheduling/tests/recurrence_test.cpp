#include "scheduling/core/recurrence.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::testing::MomentBuilder;
using pdr::testing::Numbered;

core::Date On(int year, unsigned month, unsigned day) {
    return core::Date::Compose(year, month, day).Value();
}

core::LocalTime Clock(unsigned hour, unsigned minute) {
    return core::LocalTime::Compose(hour, minute).Value();
}

core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour, unsigned minute) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, minute).Build();
}

core::TimeRange Window(core::Instant from, core::Instant to) {
    return core::TimeRange::Compose(from, to).Value();
}

/// Европа/Берлин 2026: вперёд 29 марта, назад 25 октября.
core::ZoneOffsets Berlin() {
    return core::ZoneOffsets::Compose(1h,
                                      {core::OffsetShift{Utc(2026, 3, 29, 1, 0), 2h},
                                       core::OffsetShift{Utc(2026, 10, 25, 1, 0), 1h}})
        .Value();
}

RecurrenceRule Weekly(std::string_view rrule) {
    return RecurrenceRule::Parse(rrule).Value();
}

/// «Каждый вторник в 18:00 по Берлину», начиная с 3 марта 2026-го.
RecurrenceSeries Tuesdays(std::string_view rrule = "FREQ=WEEKLY;BYDAY=TU;COUNT=8") {
    return RecurrenceSeries::Compose(Numbered<core::SeriesId>(7),
                                     Numbered<core::TenantId>(1),
                                     Numbered<core::PersonId>(10),
                                     {Numbered<core::PersonId>(20)},
                                     Weekly(rrule),
                                     On(2026, 3, 3),
                                     Clock(18, 0),
                                     core::TimeZone::Parse("Europe/Berlin").value(),
                                     60min)
        .Value();
}

std::vector<core::Date> DatesOf(const std::vector<Occurrence>& found) {
    std::vector<core::Date> dates;
    dates.reserve(found.size());
    for (const auto& occurrence : found) {
        dates.push_back(occurrence.on);
    }
    return dates;
}

const auto kYear = Window(Utc(2026, 1, 1, 0, 0), Utc(2026, 12, 31, 0, 0));

}  // namespace

TEST(RecurrenceRule, ParsesWhatTutoringNeeds) {
    const auto weekly = RecurrenceRule::Parse("FREQ=WEEKLY;BYDAY=TU;COUNT=10");
    ASSERT_TRUE(weekly.HasValue());
    EXPECT_EQ(weekly.Value().Interval(), 1);
    EXPECT_EQ(weekly.Value().Days(), std::vector{core::Weekday::kTuesday});
    EXPECT_EQ(std::get<Count>(weekly.Value().Ends()).times, 10);

    const auto fortnightly =
        RecurrenceRule::Parse("FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;UNTIL=20261231");
    ASSERT_TRUE(fortnightly.HasValue());
    EXPECT_EQ(fortnightly.Value().Interval(), 2);
    EXPECT_EQ(fortnightly.Value().Days().size(), 2U);
    EXPECT_EQ(std::get<Until>(fortnightly.Value().Ends()).date, On(2026, 12, 31));
}

/// ВСЁ, ЧЕГО МЫ НЕ ПОДДЕРЖИВАЕМ, ОТКЛОНЯЕТСЯ ВСЛУХ.
///
/// Разбор, молча пропускающий непонятую часть, отдаёт расписание, которого
/// человек не просил, и узнаёт он об этом на занятии, куда никто не пришёл.
TEST(RecurrenceRule, RefusesTheRestOfTheStandardByName) {
    for (const auto rrule : {"FREQ=MONTHLY;BYDAY=TU;COUNT=5",
                             "FREQ=DAILY;COUNT=5",
                             "FREQ=WEEKLY;BYDAY=TU;BYSETPOS=1;COUNT=5",
                             "FREQ=WEEKLY;BYDAY=TU;WKST=MO;COUNT=5",
                             "FREQ=WEEKLY;BYMONTHDAY=13;COUNT=5",
                             "FREQ=WEEKLY;BYDAY=XX;COUNT=5"}) {
        const auto refused = RecurrenceRule::Parse(rrule);
        ASSERT_FALSE(refused.HasValue()) << rrule;
        EXPECT_EQ(refused.Failure().Code(), "recurrence_rule_unsupported") << rrule;
    }
}

TEST(RecurrenceRule, DemandsExactlyOneEnding) {
    const auto neither = RecurrenceRule::Parse("FREQ=WEEKLY;BYDAY=TU");
    ASSERT_FALSE(neither.HasValue());
    EXPECT_EQ(neither.Failure().Code(), "recurrence_no_ending");

    const auto both = RecurrenceRule::Parse("FREQ=WEEKLY;BYDAY=TU;COUNT=5;UNTIL=20261231");
    ASSERT_FALSE(both.HasValue());
    EXPECT_EQ(both.Failure().Code(), "recurrence_two_endings");
}

TEST(RecurrenceRule, DemandsAFrequencyAndADay) {
    EXPECT_EQ(RecurrenceRule::Parse("BYDAY=TU;COUNT=5").Failure().Code(),
              "recurrence_frequency_missing");
    EXPECT_EQ(RecurrenceRule::Parse("FREQ=WEEKLY;COUNT=5").Failure().Code(),
              "recurrence_days_empty");
    EXPECT_EQ(RecurrenceRule::Parse("").Failure().Code(), "recurrence_rule_empty");
}

TEST(RecurrenceRule, WritesBackWhatItRead) {
    for (const auto rrule : {"FREQ=WEEKLY;BYDAY=TU;COUNT=10",
                             "FREQ=WEEKLY;INTERVAL=2;BYDAY=TU,TH;COUNT=6",
                             "FREQ=WEEKLY;BYDAY=MO;UNTIL=20261231"}) {
        EXPECT_EQ(RecurrenceRule::Parse(rrule).Value().ToRRule(), rrule);
    }
}

TEST(Expand, PutsALessonOnEveryNamedDay) {
    const auto found = Expand(Tuesdays(), kYear, Berlin());

    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().size(), 8U);
    EXPECT_EQ(found.Value().front().on, On(2026, 3, 3));
    EXPECT_EQ(found.Value().back().on, On(2026, 4, 21));
    for (const auto& occurrence : found.Value()) {
        EXPECT_EQ(occurrence.on.DayOfWeek(), core::Weekday::kTuesday);
        EXPECT_EQ(occurrence.duration, 60min);
    }
}

TEST(Expand, EveryOtherWeekSkipsTheWeekBetween) {
    const auto found = Expand(Tuesdays("FREQ=WEEKLY;INTERVAL=2;BYDAY=TU;COUNT=3"), kYear, Berlin());

    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(DatesOf(found.Value()),
              (std::vector{On(2026, 3, 3), On(2026, 3, 17), On(2026, 3, 31)}));
}

TEST(Expand, UntilEndsTheSeriesOnItsDayInclusive) {
    const auto found = Expand(Tuesdays("FREQ=WEEKLY;BYDAY=TU;UNTIL=20260324"), kYear, Berlin());

    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(DatesOf(found.Value()),
              (std::vector{On(2026, 3, 3), On(2026, 3, 10), On(2026, 3, 17), On(2026, 3, 24)}));
}

TEST(Expand, ShowsOnlyWhatFallsIntoTheWindow) {
    const auto narrow = Window(Utc(2026, 3, 15, 0, 0), Utc(2026, 3, 22, 0, 0));
    const auto found = Expand(Tuesdays(), narrow, Berlin());

    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(DatesOf(found.Value()), std::vector{On(2026, 3, 17)});
}

/// ГЛАВНЫЙ ТЕСТ: СЕРИЯ ЧЕРЕЗ ПЕРЕВОД ЧАСОВ ОСТАЁТСЯ НА СВОЁМ МЕСТЕ ПО МЕСТНЫМ ЧАСАМ.
///
/// «Каждый вторник в 18:00» — это утверждение про часы репетитора. До перевода
/// 18:00 в Берлине — это 17:00 UTC, после — 16:00 UTC. Серия, хранящая UTC,
/// после воскресной ночи начнётся в 19:00 у репетитора, и объяснять это ученику
/// будет он.
TEST(Expand, KeepsItsLocalTimeAcrossTheClockChange) {
    const auto found = Expand(Tuesdays(), kYear, Berlin());
    ASSERT_TRUE(found.HasValue());

    for (const auto& occurrence : found.Value()) {
        const auto local = core::ToLocal(occurrence.starts_at, Berlin());
        EXPECT_EQ(local.AtTime(), Clock(18, 0))
            << "дата " << occurrence.on.Year() << "-" << occurrence.on.Month() << "-"
            << occurrence.on.Day();
        EXPECT_EQ(local.OnDate(), occurrence.on);
        EXPECT_EQ(occurrence.placement, Placement::kExact);
    }

    EXPECT_EQ(found.Value()[3].on, On(2026, 3, 24));
    EXPECT_EQ(found.Value()[3].starts_at, Utc(2026, 3, 24, 17, 0));
    EXPECT_EQ(found.Value()[4].on, On(2026, 3, 31));
    EXPECT_EQ(found.Value()[4].starts_at, Utc(2026, 3, 31, 16, 0));
}

TEST(Expand, AutumnMovesTheSeriesTheOtherWay) {
    const auto autumn = RecurrenceSeries::Compose(Numbered<core::SeriesId>(7),
                                                  Numbered<core::TenantId>(1),
                                                  Numbered<core::PersonId>(10),
                                                  {Numbered<core::PersonId>(20)},
                                                  Weekly("FREQ=WEEKLY;BYDAY=TU;COUNT=3"),
                                                  On(2026, 10, 20),
                                                  Clock(18, 0),
                                                  core::TimeZone::Parse("Europe/Berlin").value(),
                                                  60min)
                            .Value();

    const auto found = Expand(autumn, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().size(), 3U);

    EXPECT_EQ(found.Value()[0].starts_at, Utc(2026, 10, 20, 16, 0));
    EXPECT_EQ(found.Value()[1].starts_at, Utc(2026, 10, 27, 17, 0));
    for (const auto& occurrence : found.Value()) {
        EXPECT_EQ(core::ToLocal(occurrence.starts_at, Berlin()).AtTime(), Clock(18, 0));
    }
}

/// Серия, попавшая на пропавший час, ОТМЕЧАЕТСЯ, а не подставляется молча.
TEST(Expand, MarksTheOccurrenceThatTheClockChangeTookAway) {
    const auto nightly = RecurrenceSeries::Compose(Numbered<core::SeriesId>(7),
                                                   Numbered<core::TenantId>(1),
                                                   Numbered<core::PersonId>(10),
                                                   {Numbered<core::PersonId>(20)},
                                                   Weekly("FREQ=WEEKLY;BYDAY=SU;COUNT=3"),
                                                   On(2026, 3, 22),
                                                   Clock(2, 30),
                                                   core::TimeZone::Parse("Europe/Berlin").value(),
                                                   60min)
                             .Value();

    const auto found = Expand(nightly, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().size(), 3U);

    EXPECT_EQ(found.Value()[0].placement, Placement::kExact);
    EXPECT_EQ(found.Value()[1].on, On(2026, 3, 29));
    EXPECT_EQ(found.Value()[1].placement, Placement::kMissingAfterClockChange);
    EXPECT_EQ(found.Value()[2].placement, Placement::kExact);
}

TEST(Expand, MarksTheOccurrenceThatHappenedTwice) {
    const auto nightly = RecurrenceSeries::Compose(Numbered<core::SeriesId>(7),
                                                   Numbered<core::TenantId>(1),
                                                   Numbered<core::PersonId>(10),
                                                   {Numbered<core::PersonId>(20)},
                                                   Weekly("FREQ=WEEKLY;BYDAY=SU;COUNT=2"),
                                                   On(2026, 10, 25),
                                                   Clock(2, 30),
                                                   core::TimeZone::Parse("Europe/Berlin").value(),
                                                   60min)
                             .Value();

    const auto found = Expand(nightly, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(found.Value()[0].placement, Placement::kTwiceOnTheClock);
    EXPECT_EQ(found.Value()[0].starts_at, Utc(2026, 10, 25, 0, 30));
}

/// ОТМЕНА ОДНОГО ВХОЖДЕНИЯ НЕ ТРОГАЕТ ОСТАЛЬНЫЕ.
TEST(Expand, ACancelledOccurrenceDisappearsAndTheRestStay) {
    const auto series =
        Tuesdays().With(RecurrenceException{On(2026, 3, 17), ExceptionKind::kCancelled}).Value();

    const auto found = Expand(series, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());

    const auto dates = DatesOf(found.Value());
    EXPECT_EQ(dates.size(), 7U);
    EXPECT_EQ(std::count(dates.begin(), dates.end(), On(2026, 3, 17)), 0);
    EXPECT_EQ(std::count(dates.begin(), dates.end(), On(2026, 3, 10)), 1);
    EXPECT_EQ(std::count(dates.begin(), dates.end(), On(2026, 3, 24)), 1);
}

/// ПЕРЕНЕСЁННОЕ ВИДНО НА НОВОМ МЕСТЕ И ОТСУТСТВУЕТ НА СТАРОМ.
TEST(Expand, AMovedOccurrenceShowsUpWhereItWasMovedTo) {
    const auto moved_to = Utc(2026, 3, 18, 9, 0);
    const auto series = Tuesdays()
                            .With(RecurrenceException{
                                On(2026, 3, 17), ExceptionKind::kMoved, moved_to, std::nullopt})
                            .Value();

    const auto found = Expand(series, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().size(), 8U);

    const auto& third = found.Value()[2];
    EXPECT_EQ(third.on, On(2026, 3, 17));
    EXPECT_EQ(third.starts_at, moved_to);
    EXPECT_EQ(third.placement, Placement::kMovedByHand);

    for (const auto& occurrence : found.Value()) {
        EXPECT_NE(occurrence.starts_at, Utc(2026, 3, 17, 17, 0))
            << "занятие осталось на прежнем расчётном месте";
    }
}

TEST(Expand, AMovedOccurrenceMayChangeItsLength) {
    const auto series = Tuesdays()
                            .With(RecurrenceException{On(2026, 3, 17),
                                                      ExceptionKind::kMoved,
                                                      Utc(2026, 3, 18, 9, 0),
                                                      Lesson::Duration{90}})
                            .Value();

    const auto found = Expand(series, kYear, Berlin());
    ASSERT_TRUE(found.HasValue());
    EXPECT_EQ(found.Value()[2].duration, 90min);
    EXPECT_EQ(found.Value()[3].duration, 60min);
}

TEST(RecurrenceSeries, RefusesAMoveWithoutAPlaceAndACancelWithOne) {
    const auto series = Tuesdays();

    EXPECT_EQ(
        series.With(RecurrenceException{On(2026, 3, 17), ExceptionKind::kMoved}).Failure().Code(),
        "recurrence_move_without_place");
    EXPECT_EQ(series
                  .With(RecurrenceException{
                      On(2026, 3, 17), ExceptionKind::kCancelled, Utc(2026, 3, 18, 9, 0)})
                  .Failure()
                  .Code(),
              "recurrence_cancel_with_place");
}

TEST(RecurrenceSeries, RefusesTwoExceptionsOnOneOccurrence) {
    const auto once =
        Tuesdays().With(RecurrenceException{On(2026, 3, 17), ExceptionKind::kCancelled}).Value();

    EXPECT_EQ(
        once.With(RecurrenceException{On(2026, 3, 17), ExceptionKind::kCancelled}).Failure().Code(),
        "recurrence_exception_repeated");
}

/// РАЗВЁРТКА ЗА ГОРИЗОНТ — ОШИБКА, А НЕ МОЛЧАЛИВОЕ УСЕЧЕНИЕ.
TEST(Expand, RefusesAWindowWiderThanTheHorizon) {
    const auto decade = Window(Utc(2026, 1, 1, 0, 0), Utc(2036, 1, 1, 0, 0));

    const auto refused = Expand(Tuesdays(), decade, Berlin());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "expansion_window_over_horizon");
}

TEST(Expand, AcceptsAWindowExactlyTheHorizonWide) {
    const auto edge = Window(Utc(2026, 1, 1, 0, 0), Utc(2026, 1, 1, 0, 0) + kDefaultHorizon);

    EXPECT_TRUE(Expand(Tuesdays(), edge, Berlin()).HasValue());
    EXPECT_FALSE(Expand(Tuesdays(),
                        Window(Utc(2026, 1, 1, 0, 0), Utc(2026, 1, 1, 0, 0) + kDefaultHorizon + 1h),
                        Berlin())
                     .HasValue());
}

/// ИЗМЕНЕНИЕ ПРАВИЛА НЕ ПЕРЕПИСЫВАЕТ ИСТОРИЮ.
///
/// Прошедшие занятия защищены не памятью развёртки, а устройством: прежняя
/// серия кончается днём разреза и после него не даёт вхождений вовсе.
TEST(RecurrenceSeries, ChangingTheRuleLeavesThePastAlone) {
    const auto before = Tuesdays("FREQ=WEEKLY;BYDAY=TU;COUNT=8");
    const auto split = before.SplitAt(
        On(2026, 3, 24), Numbered<core::SeriesId>(8), Weekly("FREQ=WEEKLY;BYDAY=TH;COUNT=4"));
    ASSERT_TRUE(split.HasValue());

    const auto& [past, future] = split.Value();

    const auto was = Expand(past, kYear, Berlin());
    ASSERT_TRUE(was.HasValue());
    EXPECT_EQ(DatesOf(was.Value()),
              (std::vector{On(2026, 3, 3), On(2026, 3, 10), On(2026, 3, 17)}));

    const auto will = Expand(future, kYear, Berlin());
    ASSERT_TRUE(will.HasValue());
    EXPECT_EQ(DatesOf(will.Value()),
              (std::vector{On(2026, 3, 26), On(2026, 4, 2), On(2026, 4, 9), On(2026, 4, 16)}));

    EXPECT_NE(past.Id(), future.Id());
    EXPECT_EQ(past.Id(), before.Id());
}

TEST(RecurrenceSeries, ASplitKeepsPastExceptionsWithThePast) {
    const auto series =
        Tuesdays().With(RecurrenceException{On(2026, 3, 10), ExceptionKind::kCancelled}).Value();

    const auto split = series.SplitAt(
        On(2026, 3, 24), Numbered<core::SeriesId>(8), Weekly("FREQ=WEEKLY;BYDAY=TU;COUNT=2"));
    ASSERT_TRUE(split.HasValue());

    EXPECT_EQ(split.Value().first.Exceptions().size(), 1U);
    EXPECT_TRUE(split.Value().second.Exceptions().empty());
}

TEST(RecurrenceSeries, RefusesASplitThatWouldLeaveNothingBehind) {
    EXPECT_EQ(
        Tuesdays()
            .SplitAt(
                On(2026, 3, 3), Numbered<core::SeriesId>(8), Weekly("FREQ=WEEKLY;BYDAY=TH;COUNT=4"))
            .Failure()
            .Code(),
        "recurrence_split_before_start");
}

TEST(RecurrenceSeries, RefusesASeriesThatEndsBeforeItStarts) {
    const auto backwards = RecurrenceSeries::Compose(Numbered<core::SeriesId>(7),
                                                     Numbered<core::TenantId>(1),
                                                     Numbered<core::PersonId>(10),
                                                     {Numbered<core::PersonId>(20)},
                                                     Weekly("FREQ=WEEKLY;BYDAY=TU;UNTIL=20260101"),
                                                     On(2026, 3, 3),
                                                     Clock(18, 0),
                                                     core::TimeZone::Parse("UTC").value(),
                                                     60min);

    ASSERT_FALSE(backwards.HasValue());
    EXPECT_EQ(backwards.Failure().Code(), "recurrence_ends_before_it_starts");
}

}  // namespace pdr::scheduling
