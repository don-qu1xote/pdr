#include "scheduling/core/time_off.hpp"

#include <chrono>
#include <optional>
#include <variant>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "scheduling/core/recurrence.hpp"

namespace pdr::scheduling {
namespace {

using pdr::testing::Numbered;

core::Date On(int year, unsigned month, unsigned day) {
    return core::Date::Compose(year, month, day).Value();
}

core::TimeZone Moscow() {
    return core::TimeZone::Parse("Europe/Moscow").value();
}

core::Result<TimeOff> Vacation(core::Date from,
                               core::Date to,
                               std::optional<TimeOffReason> reason = std::nullopt) {
    return TimeOff::Compose(Numbered<core::TimeOffId>(1),
                            Numbered<core::TenantId>(1),
                            Numbered<core::PersonId>(10),
                            from,
                            to,
                            reason,
                            Moscow(),
                            Numbered<core::PersonId>(10));
}

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: период заводится задним числом.
///
/// Проверяется не поблажка, а УСТРОЙСТВО: `Compose` не получает «сейчас» ни в
/// каком виде, и запретить прошлое ему просто нечем. Человек заболел в
/// понедельник и дошёл до телефона в среду — обычный случай, а не обход правила.
TEST(TimeOffTest, APeriodIsDeclaredAfterTheFact) {
    const auto passed = Vacation(On(2020, 1, 1), On(2020, 1, 14));

    ASSERT_TRUE(passed.HasValue());
    EXPECT_EQ(passed.Value().Days(), 14);
    EXPECT_TRUE(passed.Value().Covers(On(2020, 1, 7)));
}

/// ПРИЧИНА НЕОБЯЗАТЕЛЬНА — и это тоже устройство, а не забытая проверка.
TEST(TimeOffTest, ThereIsNoReasonToGiveAndThatIsFine) {
    const auto silent = Vacation(On(2026, 8, 1), On(2026, 8, 14));

    ASSERT_TRUE(silent.HasValue());
    EXPECT_FALSE(silent.Value().Reason().has_value());

    const auto named = Vacation(On(2026, 8, 1), On(2026, 8, 14), TimeOffReason::kSickness);
    ASSERT_TRUE(named.HasValue());
    EXPECT_EQ(named.Value().Reason(), TimeOffReason::kSickness);
}

TEST(TimeOffTest, APeriodThatEndsBeforeItStartsIsRefused) {
    const auto backwards = Vacation(On(2026, 8, 14), On(2026, 8, 1));

    ASSERT_FALSE(backwards.HasValue());
    EXPECT_EQ(backwards.Failure().Code(), "time_off_ends_before_it_starts");
}

/// Потолок длины отделяет осмысленное от бессмысленного: перерыв длиннее года с
/// небольшим не значит ничего — расписания на такую даль ещё нет.
TEST(TimeOffTest, APeriodLongerThanAYearMeansNothingAndIsRefused) {
    const auto endless = Vacation(On(2026, 8, 1), On(2027, 10, 14));

    ASSERT_FALSE(endless.HasValue());
    EXPECT_EQ(endless.Failure().Code(), "time_off_too_long");

    /// А годовой перерыв бывает настоящим — декрет, переезд, армия.
    EXPECT_TRUE(Vacation(On(2026, 8, 1), On(2027, 7, 31)).HasValue());
}

TEST(TimeOffTest, BothEndsAreInsideAndTheDayAfterIsNot) {
    const auto period = Vacation(On(2026, 8, 1), On(2026, 8, 14)).Value();

    EXPECT_TRUE(period.Covers(On(2026, 8, 1)));
    EXPECT_TRUE(period.Covers(On(2026, 8, 14)));
    EXPECT_FALSE(period.Covers(On(2026, 7, 31)));
    EXPECT_FALSE(period.Covers(On(2026, 8, 15)));
    EXPECT_EQ(period.ResumesOn(), On(2026, 8, 15));
}

/// СДВИГ МЕРЯЕТСЯ ЦЕЛЫМИ НЕДЕЛЯМИ, и берётся наименьшее их число, которое
/// кладёт занятие СТРОГО после перерыва. Иначе «каждый вторник» превращается в
/// «каждую пятницу», и это уже не та серия, отложенная, а другая.
TEST(TimeOffTest, TheShiftIsWholeWeeksAndClearsThePeriod) {
    EXPECT_EQ(WeeksOver(Vacation(On(2026, 8, 1), On(2026, 8, 1)).Value()), 1);
    EXPECT_EQ(WeeksOver(Vacation(On(2026, 8, 1), On(2026, 8, 7)).Value()), 1);
    EXPECT_EQ(WeeksOver(Vacation(On(2026, 8, 1), On(2026, 8, 8)).Value()), 2);
    EXPECT_EQ(WeeksOver(Vacation(On(2026, 8, 1), On(2026, 8, 14)).Value()), 2);
    EXPECT_EQ(WeeksOver(Vacation(On(2026, 8, 1), On(2026, 8, 15)).Value()), 3);
}

TEST(TimeOffTest, EveryDecisionAndReasonHasItsWordAndReadsBack) {
    for (const auto reason :
         {TimeOffReason::kVacation, TimeOffReason::kSickness, TimeOffReason::kBreak}) {
        EXPECT_EQ(ParseTimeOffReason(Name(reason)), reason);
    }
    for (const auto decision :
         {TimeOffDecision::kCancel, TimeOffDecision::kPostpone, TimeOffDecision::kKeep}) {
        EXPECT_EQ(ParseTimeOffDecision(Name(decision)), decision);
    }
    for (const auto decision : {SeriesDecision::kSkip, SeriesDecision::kShift}) {
        EXPECT_EQ(ParseSeriesDecision(Name(decision)), decision);
    }
    EXPECT_FALSE(ParseTimeOffReason("отпуск").has_value());
    EXPECT_FALSE(ParseSeriesDecision("shift ").has_value());
}

/// УМОЛЧАНИЕ ПО СЕРИЯМ — ПРОПУСК, и это видно в самом типе: первое значение
/// перечисления и есть то, что достаётся полю по умолчанию.
TEST(TimeOffTest, SkippingIsWhatTheSeriesGetByDefault) {
    struct Decided final {
        SeriesDecision series;
    };

    EXPECT_EQ(Decided{}.series, SeriesDecision::kSkip);
}

RecurrenceSeries EveryTuesday(core::Date starts_on, Ending ending) {
    return RecurrenceSeries::Compose(
               Numbered<core::SeriesId>(1),
               Numbered<core::TenantId>(1),
               Numbered<core::PersonId>(10),
               {Numbered<core::PersonId>(20)},
               RecurrenceRule::Compose(1, {core::Weekday::kTuesday}, ending).Value(),
               starts_on,
               core::LocalTime::Compose(18, 0).Value(),
               Moscow(),
               std::chrono::minutes{60})
        .Value();
}

/// ДАТЫ ВХОЖДЕНИЙ СЧИТАЮТСЯ БЕЗ ЗОНЫ ВОВСЕ — и в этом весь смысл: порта правил
/// зоны в дереве нет, а пропустить занятия отпуска надо.
TEST(TimeOffTest, TheDatesInsideThePeriodAreFoundWithoutAnyTimeZoneRules) {
    /// Вторники августа 2026-го: 4, 11, 18, 25.
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Count{8}});

    const auto inside = OccurrenceDates(series, On(2026, 8, 5), On(2026, 8, 20));

    ASSERT_EQ(inside.size(), 2U);
    EXPECT_EQ(inside.front(), On(2026, 8, 11));
    EXPECT_EQ(inside.back(), On(2026, 8, 18));
}

TEST(TimeOffTest, AnAlreadyCancelledOccurrenceIsNotOfferedForCancellingAgain) {
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Count{8}})
                            .With(RecurrenceException{On(2026, 8, 11), ExceptionKind::kCancelled})
                            .Value();

    const auto inside = OccurrenceDates(series, On(2026, 8, 5), On(2026, 8, 20));

    ASSERT_EQ(inside.size(), 1U);
    EXPECT_EQ(inside.front(), On(2026, 8, 18));
}

TEST(TimeOffTest, TheCountIsSpentByCancelledOccurrencesToo) {
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Count{8}})
                            .With(RecurrenceException{On(2026, 8, 11), ExceptionKind::kCancelled})
                            .Value();

    EXPECT_EQ(OccurrencesBefore(series, On(2026, 8, 19)), 3);
}

/// СЕРИЯ ВОЗОБНОВЛЯЕТСЯ ПОСЛЕ ПЕРЕРЫВА, НИЧЕГО НЕ ПОТЕРЯВ: обещали восемь
/// занятий — их и будет восемь, просто последние позже.
TEST(TimeOffTest, TheShiftedSeriesKeepsItsWeekdayAndItsPromisedCount) {
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Count{8}});
    const auto period = Vacation(On(2026, 8, 10), On(2026, 8, 23)).Value();

    const auto pieces =
        series.ShiftPast(period.From(), WeeksOver(period), Numbered<core::SeriesId>(2));

    ASSERT_TRUE(pieces.HasValue());
    ASSERT_EQ(pieces.Value().size(), 2U);

    const auto& before = pieces.Value().front();
    const auto& after = pieces.Value().back();

    EXPECT_TRUE(before.Id() == series.Id()) << "прежняя серия сменила идентификатор";
    EXPECT_EQ(std::get<Until>(before.Rule().Ends()).date, On(2026, 8, 9));

    EXPECT_TRUE(after.Id() == Numbered<core::SeriesId>(2));
    EXPECT_EQ(after.StartsOn(), On(2026, 8, 24));
    EXPECT_EQ(after.StartsOn().DayOfWeek(), period.From().DayOfWeek())
        << "сдвиг на целые недели обязан оставить день недели тем же";

    /// До перерыва прошёл один вторник — 4 августа: 11-е уже внутри. Значит
    /// остаётся семь, и обещанные восемь занятий человек всё-таки получит.
    EXPECT_EQ(std::get<Count>(after.Rule().Ends()).times, 7);

    const auto resumed = OccurrenceDates(after, On(2026, 8, 24), On(2026, 9, 30));
    ASSERT_FALSE(resumed.empty());
    EXPECT_EQ(resumed.front(), On(2026, 8, 25))
        << "первое занятие после перерыва встало не на вторник";
}

TEST(TimeOffTest, AnUntilEndingTravelsTheSameWholeWeeks) {
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Until{On(2026, 10, 6)}});
    const auto period = Vacation(On(2026, 8, 10), On(2026, 8, 23)).Value();

    const auto pieces =
        series.ShiftPast(period.From(), WeeksOver(period), Numbered<core::SeriesId>(2));

    ASSERT_TRUE(pieces.HasValue());
    EXPECT_EQ(std::get<Until>(pieces.Value().back().Rule().Ends()).date, On(2026, 10, 20));
}

/// Серия, которая до перерыва и не начиналась, резать нечего: она просто
/// начинается позже, и второй серии не нужно.
TEST(TimeOffTest, ASeriesThatHadNotStartedYetJustStartsLater) {
    const auto series = EveryTuesday(On(2026, 8, 11), Ending{Count{8}});
    const auto period = Vacation(On(2026, 8, 10), On(2026, 8, 23)).Value();

    const auto pieces =
        series.ShiftPast(period.From(), WeeksOver(period), Numbered<core::SeriesId>(2));

    ASSERT_TRUE(pieces.HasValue());
    ASSERT_EQ(pieces.Value().size(), 1U);
    EXPECT_TRUE(pieces.Value().front().Id() == series.Id());
    EXPECT_EQ(pieces.Value().front().StartsOn(), On(2026, 8, 25));
    EXPECT_EQ(std::get<Count>(pieces.Value().front().Rule().Ends()).times, 8);
}

TEST(TimeOffTest, ASeriesThatEndedBeforeThePeriodHasNothingToShift) {
    const auto series = EveryTuesday(On(2026, 5, 1), Ending{Until{On(2026, 6, 30)}});
    const auto period = Vacation(On(2026, 8, 10), On(2026, 8, 23)).Value();

    const auto refused =
        series.ShiftPast(period.From(), WeeksOver(period), Numbered<core::SeriesId>(2));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "recurrence_shift_after_the_end");
}

TEST(TimeOffTest, AShiftOfLessThanAWeekIsRefused) {
    const auto series = EveryTuesday(On(2026, 8, 1), Ending{Count{8}});

    const auto refused = series.ShiftPast(On(2026, 8, 10), 0, Numbered<core::SeriesId>(2));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "recurrence_shift_not_whole_weeks");
}

}  // namespace
}  // namespace pdr::scheduling
