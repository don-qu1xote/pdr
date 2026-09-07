#include "scheduling/application/resolve_time_off.hpp"

#include <algorithm>
#include <chrono>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/time_off_applied.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_scheduling.hpp"
#include "scheduling/application/declare_time_off.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeLessonHistory;
using pdr::scheduling::testing::FakeLessons;
using pdr::scheduling::testing::FakeLocalDays;
using pdr::scheduling::testing::FakeSeries;
using pdr::scheduling::testing::FakeTimeOffs;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::FakeClock;
using pdr::testing::FakeIdGenerator;
using pdr::testing::MomentBuilder;
using pdr::testing::Numbered;

constexpr std::int64_t kPriceMinor = 400000;

core::CurrencyCode Roubles() {
    return core::CurrencyCode::Parse("RUB").value();
}

core::Money Price() {
    return core::Money::FromMinorUnits(kPriceMinor, Roubles());
}

core::TenantId Tenant() {
    return Numbered<core::TenantId>(1);
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

core::PersonId Student() {
    return Numbered<core::PersonId>(20);
}

core::PersonId Another() {
    return Numbered<core::PersonId>(21);
}

core::Date On(int year, unsigned month, unsigned day) {
    return core::Date::Compose(year, month, day).Value();
}

core::TimeZone Moscow() {
    return core::TimeZone::Parse("Europe/Moscow").value();
}

core::Instant At(unsigned month, unsigned day, unsigned hour) {
    return MomentBuilder{}.Utc(2026, month, day).At(hour, 0).Build();
}

/// Серия по вторникам с первого августа: вхождения 4-го и 11-го попадают в
/// перерыв, 18-е и дальше — нет.
RecurrenceSeries Weekly(Ending ending) {
    return RecurrenceSeries::Compose(
               Numbered<core::SeriesId>(1),
               Tenant(),
               Tutor(),
               {Student()},
               RecurrenceRule::Compose(1, {core::Weekday::kTuesday}, ending).Value(),
               On(2026, 8, 1),
               core::LocalTime::Compose(18, 0).Value(),
               Moscow(),
               std::chrono::minutes{60})
        .Value();
}

/// Политика, которую отдаёт порт, и счётчик обращений к ней. Счётчик здесь
/// главный: «удержания не будет» проверяется не только суммой, но и тем, что
/// политику ВООБЩЕ не спрашивали.
class FakePolicies final : public ports::CancellationPolicies {
public:
    core::Result<CancellationPolicy> Of(const core::TenantId&) const override {
        ++asked;
        return CancellationPolicy::Compose(
            std::chrono::duration_cast<CancellationPolicy::Window>(24h),
            core::Percent::Compose(50).value(),
            core::Percent::Compose(100).value(),
            1);
    }

    mutable int asked{0};
};

class ResolveTimeOffTest : public ::testing::Test {
protected:
    ResolveTimeOffTest() {
        Book(1, Student(), At(8, 4, 15));
        Book(2, Student(), At(8, 11, 15));
        Book(3, Another(), At(8, 5, 15));
        Book(4, Student(), At(9, 1, 15));
    }

    void Book(int number, const core::PersonId& student, core::Instant starts_at) {
        EXPECT_TRUE(lessons_
                        .Save(LessonBuilder{}
                                  .Id(Numbered<core::LessonId>(number))
                                  .InTenant(Tenant())
                                  .Between(Tutor(), student)
                                  .StartingAt(starts_at)
                                  .Build())
                        .HasValue());
    }

    core::TimeOffId Declare(const core::PersonId& person) {
        const DeclareTimeOff declaring{periods_, lessons_, days_, ids_, clock_, bus_};
        const auto declared = declaring.Execute(DeclareTimeOff::Request{
            Tenant(), person, person, On(2026, 8, 3), On(2026, 8, 16), std::nullopt, Moscow()});
        EXPECT_TRUE(declared.HasValue());
        return declared.Value().period.Id();
    }

    ResolveTimeOff Resolving() {
        return ResolveTimeOff{
            periods_, lessons_, history_, series_, policies_, days_, ids_, clock_, bus_};
    }

    ResolveTimeOff::Request Deciding(const core::TimeOffId& period,
                                     TimeOffDecision decision,
                                     SeriesDecision series = SeriesDecision::kSkip) const {
        return ResolveTimeOff::Request{Tenant(), period, Tutor(), decision, series, Price()};
    }

    std::vector<events::scheduling::TimeOffApplied> applied_;
    std::vector<events::scheduling::LessonCancelled> cancelled_;

    void Listen() {
        bus_.Subscribe<events::scheduling::TimeOffApplied>(
            [this](const events::scheduling::TimeOffApplied& event) { applied_.push_back(event); });
        bus_.Subscribe<events::scheduling::LessonCancelled>(
            [this](const events::scheduling::LessonCancelled& event) {
                cancelled_.push_back(event);
            });
    }

    FakeLocalDays days_{3h};
    FakeClock clock_{At(7, 20, 12)};
    FakeIdGenerator ids_;
    FakeLessons lessons_;
    FakeLessonHistory history_;
    FakeSeries series_;
    FakeTimeOffs periods_;
    FakePolicies policies_;
    events::InMemoryBus bus_;
};

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: одно решение применяется КО ВСЕМ занятиям периода,
/// и удержания не начисляется ни на одном — политику при этом даже не
/// спрашивают, потому что спрашивать не о чем.
TEST_F(ResolveTimeOffTest, OneDecisionCancelsEveryLessonAndNothingIsRetained) {
    const auto period = Declare(Tutor());
    Listen();

    const auto applied = Resolving().Execute(Deciding(period, TimeOffDecision::kCancel));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().lessons, 3);
    EXPECT_EQ(applied.Value().cancelled, 3);
    EXPECT_EQ(policies_.asked, 0) << "политику спросили там, где она ничего не решает";

    for (const auto number : {1, 2, 3}) {
        const auto kept = lessons_.Find(Tenant(), Numbered<core::LessonId>(number));
        ASSERT_TRUE(kept.has_value());
        EXPECT_EQ(kept->State(), LessonState::kCancelled);
    }
    const auto untouched = lessons_.Find(Tenant(), Numbered<core::LessonId>(4));
    ASSERT_TRUE(untouched.has_value());
    EXPECT_EQ(untouched->State(), LessonState::kPlanned) << "занятие вне периода тоже отменили";

    ASSERT_EQ(cancelled_.size(), 3U);
    for (const auto& event : cancelled_) {
        EXPECT_EQ(event.retained, core::Money::FromMinorUnits(0, Roubles()));
        EXPECT_EQ(event.reason, RetentionReason::kTutorCancelled);
        EXPECT_EQ(event.by, CancelledBy::kTutor);
        ASSERT_TRUE(event.time_off.has_value());
        EXPECT_TRUE(*event.time_off == period);
    }
}

/// ВТОРАЯ ГЛАВНАЯ ПРОВЕРКА: сказать надо ПО РАЗУ НА ЧЕЛОВЕКА, а не по разу на
/// занятие. Ученик, потерявший два занятия, стоит в списке один раз.
TEST_F(ResolveTimeOffTest, EveryoneIsToldOnceRatherThanOncePerLesson) {
    const auto period = Declare(Tutor());
    Listen();

    ASSERT_TRUE(Resolving().Execute(Deciding(period, TimeOffDecision::kCancel)).HasValue());

    ASSERT_EQ(applied_.size(), 1U) << "решение по перерыву издало больше одного события";
    const auto& tell = applied_.front().tell;
    EXPECT_EQ(tell.size(), 3U) << "получателей столько же, сколько занятий, а не сколько людей";
    EXPECT_EQ(std::count(tell.begin(), tell.end(), Student()), 1)
        << "ученик, потерявший два занятия, получит два письма";
    EXPECT_EQ(std::count(tell.begin(), tell.end(), Another()), 1);
    EXPECT_EQ(std::count(tell.begin(), tell.end(), Tutor()), 1);
    EXPECT_EQ(applied_.front().lessons, 3);
    EXPECT_EQ(applied_.front().decision, TimeOffDecision::kCancel);
}

/// ПЕРЕНОС — ЦЕЛЫМИ НЕДЕЛЯМИ: вторник в 18:00 переезжает на вторник в 18:00.
TEST_F(ResolveTimeOffTest, PostponingMovesEveryLessonPastThePeriodKeepingItsWeekday) {
    const auto period = Declare(Tutor());

    const auto applied = Resolving().Execute(Deciding(period, TimeOffDecision::kPostpone));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().postponed, 3);

    const auto first = lessons_.Find(Tenant(), Numbered<core::LessonId>(1));
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(first->StartsAt(), At(8, 18, 15)) << "занятие переехало не на две недели вперёд";
    EXPECT_EQ(first->State(), LessonState::kPlanned) << "перенос поменял состояние занятия";
    EXPECT_EQ(lessons_.Kept().size(), 4U) << "перенос завёл второе занятие";
}

TEST_F(ResolveTimeOffTest, KeepingThemChangesNothingButIsStillADecision) {
    const auto period = Declare(Tutor());
    Listen();

    const auto applied = Resolving().Execute(Deciding(period, TimeOffDecision::kKeep));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().lessons, 3);
    EXPECT_EQ(applied.Value().cancelled, 0);
    EXPECT_EQ(applied.Value().postponed, 0);
    EXPECT_TRUE(cancelled_.empty());
    ASSERT_EQ(applied_.size(), 1U) << "о решении «оставить как есть» тоже надо сказать";
}

/// КАНИКУЛЫ УЧЕНИКА — ТОТ ЖЕ МЕХАНИЗМ, но сторона другая, и удержание считается
/// по политике: платит тот, кто отменил.
TEST_F(ResolveTimeOffTest, TheStudentsHolidayIsCancelledFromTheStudentsSide) {
    const auto period = Declare(Student());
    clock_.SetNow(At(8, 4, 14));
    Listen();

    const auto applied = Resolving().Execute(Deciding(period, TimeOffDecision::kCancel));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().cancelled, 2) << "чужое занятие отменили вместе с каникулами";
    EXPECT_GT(policies_.asked, 0) << "у ученика удержание считает политика, и спросить её надо";

    ASSERT_EQ(cancelled_.size(), 2U);
    EXPECT_EQ(cancelled_.front().by, CancelledBy::kStudent);
    EXPECT_EQ(cancelled_.front().retained, core::Money::FromMinorUnits(kPriceMinor / 2, Roubles()));
    EXPECT_EQ(cancelled_.front().reason, RetentionReason::kLateCancellation);
}

/// СЕРИЯ ПРОПУСКАЕТ ПЕРИОД — умолчание. Правило серии не трогается вовсе: после
/// перерыва она идёт как шла.
TEST_F(ResolveTimeOffTest, ByDefaultTheSeriesSkipsThePeriodAndKeepsItsRule) {
    const auto series = Weekly(Ending{Count{8}});
    ASSERT_TRUE(series_.Create(series).HasValue());
    const auto period = Declare(Tutor());

    const auto applied = Resolving().Execute(Deciding(period, TimeOffDecision::kKeep));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().series, 1);

    const auto kept = series_.Find(Tenant(), series.Id());
    ASSERT_TRUE(kept.has_value());
    EXPECT_TRUE(kept->Rule() == series.Rule()) << "пропуск переписал правило серии";
    ASSERT_EQ(kept->Exceptions().size(), 2U);
    EXPECT_EQ(kept->Exceptions().front().occurrence_on, On(2026, 8, 4));
    EXPECT_EQ(kept->Exceptions().front().kind, ExceptionKind::kCancelled);
    EXPECT_EQ(kept->Exceptions().back().occurrence_on, On(2026, 8, 11));
}

/// СЕРИЯ СДВИГАЕТСЯ ЦЕЛИКОМ И ВОЗОБНОВЛЯЕТСЯ ПОСЛЕ ПЕРЕРЫВА, ничего не потеряв.
TEST_F(ResolveTimeOffTest, TheShiftedSeriesResumesAfterThePeriodWithNothingLost) {
    const auto series = Weekly(Ending{Count{8}});
    ASSERT_TRUE(series_.Create(series).HasValue());
    const auto period = Declare(Tutor());

    const auto applied =
        Resolving().Execute(Deciding(period, TimeOffDecision::kKeep, SeriesDecision::kShift));

    ASSERT_TRUE(applied.HasValue());
    EXPECT_EQ(applied.Value().series, 1);

    const auto before = series_.Find(Tenant(), series.Id());
    ASSERT_TRUE(before.has_value());
    EXPECT_EQ(std::get<Until>(before->Rule().Ends()).date, On(2026, 8, 2))
        << "прежняя серия не укоротилась днём перед перерывом";

    const auto tail = series_.Of(Tenant(), Tutor());
    ASSERT_EQ(tail.size(), 2U) << "остаток серии не завёлся";

    const auto after = series_.Find(Tenant(), tail.back());
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->StartsOn(), On(2026, 8, 17));
    EXPECT_EQ(std::get<Count>(after->Rule().Ends()).times, 8)
        << "занятия перерыва потерялись вместо того, чтобы уехать в конец";

    const auto resumed = OccurrenceDates(*after, On(2026, 8, 17), On(2026, 9, 1));
    ASSERT_FALSE(resumed.empty());
    EXPECT_EQ(resumed.front(), On(2026, 8, 18)) << "серия возобновилась не во вторник";
}

/// Решение принимают ОДИН раз: занятия уже отменены, и применить второе решение
/// не к чему.
TEST_F(ResolveTimeOffTest, ADecisionIsMadeOnceAndNotTakenBack) {
    const auto period = Declare(Tutor());
    ASSERT_TRUE(Resolving().Execute(Deciding(period, TimeOffDecision::kCancel)).HasValue());

    const auto again = Resolving().Execute(Deciding(period, TimeOffDecision::kPostpone));

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "time_off_already_decided");
}

TEST_F(ResolveTimeOffTest, WhatWasNeverDeclaredIsNotResolved) {
    const auto missing =
        Resolving().Execute(Deciding(Numbered<core::TimeOffId>(999), TimeOffDecision::kCancel));

    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Failure().Code(), "time_off_not_found");
    EXPECT_EQ(bus_.Published(), 0U);
}

}  // namespace
}  // namespace pdr::scheduling
