#include "scheduling/application/declare_time_off.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/time_off_declared.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_scheduling.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeLessons;
using pdr::scheduling::testing::FakeLocalDays;
using pdr::scheduling::testing::FakeTimeOffs;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::FakeClock;
using pdr::testing::FakeIdGenerator;
using pdr::testing::MomentBuilder;
using pdr::testing::Numbered;

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

class DeclareTimeOffTest : public ::testing::Test {
protected:
    DeclareTimeOffTest() {
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

    DeclareTimeOff Declaring() {
        return DeclareTimeOff{periods_, lessons_, days_, ids_, clock_, bus_};
    }

    DeclareTimeOff::Request Asking(const core::PersonId& person,
                                   core::Date from,
                                   core::Date to) const {
        return DeclareTimeOff::Request{Tenant(), person, person, from, to, std::nullopt, Moscow()};
    }

    /// Смещение Москвы: полночь третьего августа по местным часам — это
    /// двадцать первое число предыдущих суток в UTC, и занятия сравниваются
    /// именно с ним.
    FakeLocalDays days_{3h};
    FakeClock clock_{At(7, 20, 12)};
    FakeIdGenerator ids_;
    FakeLessons lessons_;
    FakeTimeOffs periods_;
    events::InMemoryBus bus_;
};

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: заведение и решение — ДВА действия, и первое сразу
/// показывает, о чём предстоит решать.
TEST_F(DeclareTimeOffTest, TheFirstActionAlreadyShowsWhatFallsInside) {
    const auto declared = Declaring().Execute(Asking(Tutor(), On(2026, 8, 3), On(2026, 8, 16)));

    ASSERT_TRUE(declared.HasValue());
    ASSERT_EQ(declared.Value().affected.size(), 3U);
    EXPECT_TRUE(declared.Value().affected[0].Id() == Numbered<core::LessonId>(1));
    EXPECT_TRUE(declared.Value().affected[1].Id() == Numbered<core::LessonId>(3));
    EXPECT_TRUE(declared.Value().affected[2].Id() == Numbered<core::LessonId>(2))
        << "занятия периода отданы не по возрастанию начала";
}

/// ЗАДНИМ ЧИСЛОМ. Часы стоят на двадцатом июля, а период заводится за январь —
/// и это проходит, потому что запрещать такое здесь нечем.
TEST_F(DeclareTimeOffTest, APeriodThatHasAlreadyPassedIsDeclaredAllTheSame) {
    const auto declared = Declaring().Execute(Asking(Tutor(), On(2026, 1, 5), On(2026, 1, 18)));

    ASSERT_TRUE(declared.HasValue());
    EXPECT_TRUE(declared.Value().affected.empty());
    EXPECT_TRUE(periods_.Find(Tenant(), declared.Value().period.Id()).has_value());
}

/// КАНИКУЛЫ УЧЕНИКА — ТОТ ЖЕ СЦЕНАРИЙ И ТА ЖЕ ЗАПИСЬ. Разветвления по роли
/// внутри нет ни одного; сторона видна из самих занятий.
TEST_F(DeclareTimeOffTest, TheStudentsHolidayGoesThroughTheVerySameScenario) {
    const auto declared = Declaring().Execute(Asking(Student(), On(2026, 8, 3), On(2026, 8, 16)));

    ASSERT_TRUE(declared.HasValue());
    ASSERT_EQ(declared.Value().affected.size(), 2U) << "чужое занятие попало в каникулы ученика";
    EXPECT_TRUE(declared.Value().affected[0].Id() == Numbered<core::LessonId>(1));
    EXPECT_TRUE(declared.Value().affected[1].Id() == Numbered<core::LessonId>(2));
}

TEST_F(DeclareTimeOffTest, TheDeclarationCarriesTheSpanInMomentsForWhoeverNeedsIt) {
    std::vector<events::scheduling::TimeOffDeclared> heard;
    bus_.Subscribe<events::scheduling::TimeOffDeclared>(
        [&heard](const events::scheduling::TimeOffDeclared& event) { heard.push_back(event); });

    const auto declared = Declaring().Execute(DeclareTimeOff::Request{Tenant(),
                                                                      Tutor(),
                                                                      Tutor(),
                                                                      On(2026, 8, 3),
                                                                      On(2026, 8, 16),
                                                                      TimeOffReason::kSickness,
                                                                      Moscow()});

    ASSERT_TRUE(declared.HasValue());
    ASSERT_EQ(heard.size(), 1U);
    EXPECT_TRUE(heard.front().person == Tutor());
    EXPECT_EQ(heard.front().from, At(8, 2, 21));
    EXPECT_EQ(heard.front().to, At(8, 16, 21));
    EXPECT_EQ(heard.front().reason, TimeOffReason::kSickness);
    EXPECT_TRUE(heard.front().envelope.occurred_at == clock_.Now());
}

/// Наложение ловит хранилище — то же, что и база. Два перерыва на одни дни
/// означали бы два решения на одно занятие.
TEST_F(DeclareTimeOffTest, TwoPeriodsOfOnePersonDoNotOverlap) {
    ASSERT_TRUE(Declaring().Execute(Asking(Tutor(), On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    const auto again = Declaring().Execute(Asking(Tutor(), On(2026, 8, 10), On(2026, 8, 20)));

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "time_off_overlaps");
    EXPECT_EQ(bus_.Published(), 1U) << "отклонённое заведение всё-таки опубликовало событие";
}

TEST_F(DeclareTimeOffTest, AnotherPersonMayBeAwayOnTheSameDays) {
    ASSERT_TRUE(Declaring().Execute(Asking(Tutor(), On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    EXPECT_TRUE(Declaring().Execute(Asking(Student(), On(2026, 8, 3), On(2026, 8, 16))).HasValue());
}

/// Отменённое занятие внутри периода — не «затронутое»: решать по нему нечего,
/// и показывать его человеку значило бы соврать.
TEST_F(DeclareTimeOffTest, WhatIsAlreadyCancelledIsNotOfferedForDecidingAgain) {
    const auto lesson = lessons_.Find(Tenant(), Numbered<core::LessonId>(1));
    ASSERT_TRUE(lesson.has_value());
    const auto cancelled =
        lesson->CancelByTutor(core::CurrencyCode::Parse("RUB").value(), Tutor(), clock_.Now());
    ASSERT_TRUE(cancelled.HasValue());
    ASSERT_TRUE(lessons_.SetState(cancelled.Value().lesson).HasValue());

    const auto declared = Declaring().Execute(Asking(Tutor(), On(2026, 8, 3), On(2026, 8, 16)));

    ASSERT_TRUE(declared.HasValue());
    EXPECT_EQ(declared.Value().affected.size(), 2U);
}

}  // namespace
}  // namespace pdr::scheduling
