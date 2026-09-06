#include "scheduling/core/participation.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "core/money.hpp"
#include "fakes/fake_clock.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::Numbered;

core::CurrencyCode Rubles() {
    return core::CurrencyCode::Parse("RUB").value();
}

core::Money Rub(std::int64_t minor) {
    return core::Money::FromMinorUnits(minor, Rubles());
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

core::PersonId Student() {
    return Numbered<core::PersonId>(20);
}

core::PersonId Second() {
    return Numbered<core::PersonId>(21);
}

CancellationPolicy Policy() {
    const auto composed =
        CancellationPolicy::Compose(std::chrono::duration_cast<CancellationPolicy::Window>(24h),
                                    core::Percent::Compose(50).value(),
                                    core::Percent::Compose(100).value(),
                                    1);
    EXPECT_TRUE(composed.HasValue());
    return composed.Value();
}

}  // namespace

/// Записавшийся человек не платил, не приходил и не выходил: всё это появляется
/// потом и по одному.
TEST(Participation, JoiningSaysOnlyThatSomeoneJoined) {
    const auto taking = Participation::Joined(Student());

    EXPECT_TRUE(taking.Person() == Student());
    EXPECT_FALSE(taking.Price().has_value()) << "цена завелась сама";
    EXPECT_EQ(taking.Payment(), PaymentState::kUnpaid);
    EXPECT_EQ(taking.Attended(), Attendance::kExpected);
    EXPECT_EQ(taking.State(), ParticipationState::kJoined);
}

/// ПУСТАЯ ЦЕНА — НЕ БЕСПЛАТНО. Бесплатное участие это назначенный ноль, и
/// отличить одно от другого обязана модель, а не комментарий рядом с ней.
TEST(Participation, NoPriceIsNotAFreeLesson) {
    const auto unnamed = Participation::Joined(Student());
    const auto free = unnamed.Priced(Rub(0));

    EXPECT_FALSE(unnamed.Price().has_value());
    ASSERT_TRUE(free.Price().has_value());
    EXPECT_EQ(free.Price()->MinorUnits(), 0);
}

/// Величина, а не изменяемый объект: переход возвращает новое участие, а старое
/// остаётся прежним.
TEST(Participation, EveryChangeReturnsANewValue) {
    const auto joined = Participation::Joined(Student());

    const auto paid = joined.Priced(Rub(150000)).Paid().Came();

    EXPECT_EQ(joined.Payment(), PaymentState::kUnpaid);
    EXPECT_EQ(joined.Attended(), Attendance::kExpected);
    EXPECT_EQ(paid.Payment(), PaymentState::kPaid);
    EXPECT_EQ(paid.Attended(), Attendance::kAttended);
    EXPECT_TRUE(paid.Person() == Student());
}

TEST(Participation, EveryStateHasAName) {
    for (const auto state : kEveryPaymentState) {
        EXPECT_FALSE(Name(state).empty());
    }
    for (const auto attendance : kEveryAttendance) {
        EXPECT_FALSE(Name(attendance).empty());
    }
    for (const auto state : kEveryParticipationState) {
        EXPECT_FALSE(Name(state).empty());
    }
}

/// ОБЯЗАТЕЛЬНОЕ ОГРАНИЧЕНИЕ ЗАДАЧИ: второго участника не записать, и отказ
/// говорит человеческими словами, а не «участников не один».
TEST(LessonParticipation, ASecondParticipantIsRefusedWithAPlainReason) {
    const auto starts = pdr::testing::MomentBuilder{}.Utc(2026, 3, 2).At(18, 0).Build();

    const auto crowded =
        Lesson::Schedule(Numbered<core::LessonId>(100),
                         Numbered<core::TenantId>(1),
                         Tutor(),
                         {Participation::Joined(Student()), Participation::Joined(Second())},
                         starts,
                         60min,
                         core::TimeZone::Parse("Europe/Moscow").value(),
                         starts - 24h);

    ASSERT_FALSE(crowded.HasValue()) << "групповое занятие записалось";
    EXPECT_EQ(crowded.Failure().Code(), "lesson_group_not_supported");
}

/// Запрет — ПРАВИЛО, а не форма модели: занятие держит вектор участий и тогда,
/// когда в нём одно. Снятие запрета правит число, а не тип.
TEST(LessonParticipation, TheModelHoldsASetEvenForOne) {
    const auto lesson = LessonBuilder{}.Build();

    ASSERT_EQ(lesson.Participants().size(), 1U);
    EXPECT_EQ(Lesson::kParticipantsForNow, 1U);
    EXPECT_TRUE(lesson.Participants().front().Person() == lesson.People().front());
}

/// ОПЛАТА СЧИТАЕТСЯ ЧЕРЕЗ УЧАСТИЕ: цена ложится на участие, а не на занятие.
TEST(LessonParticipation, HoldingALessonPricesTheParticipationAndMarksAttendance) {
    const auto lesson = LessonBuilder{}.Build();

    const auto held = lesson.MarkHeld(Rub(150000), Tutor(), lesson.StartsAt() + 60min);

    ASSERT_TRUE(held.HasValue()) << held.Failure().Code();
    ASSERT_EQ(held.Value().lesson.Participants().size(), 1U);
    const auto& taking = held.Value().lesson.Participants().front();
    ASSERT_TRUE(taking.Price().has_value()) << "цену занятия некуда положить";
    EXPECT_EQ(taking.Price()->MinorUnits(), 150000);
    EXPECT_EQ(taking.Attended(), Attendance::kAttended);
}

/// ПРОГРЕСС СЧИТАЕТСЯ ПО УЧАСТНИКУ: неявка — факт про человека, и лежит она на
/// участии. Состояние занятия при этом меняется как и раньше.
TEST(LessonParticipation, ANoShowIsRecordedOnTheParticipationNotOnlyOnTheLesson) {
    const auto lesson = LessonBuilder{}.Build();

    const auto missed =
        lesson.MarkNoShow(Policy(), Rub(150000), Tutor(), lesson.StartsAt() + 60min);

    ASSERT_TRUE(missed.HasValue()) << missed.Failure().Code();
    EXPECT_EQ(missed.Value().lesson.State(), LessonState::kNoShow);
    EXPECT_EQ(missed.Value().lesson.Participants().front().Attended(), Attendance::kMissed);
}

/// ОБЯЗАТЕЛЬНОЕ РАЗЛИЧЕНИЕ ЗАДАЧИ: последнего участника не вывести — это
/// отмена, а не выход. Сегодня участник всегда последний, поэтому выход не
/// проходит никогда, и это верно, а не временно.
TEST(LessonParticipation, WithdrawingTheLastParticipantIsACancellationAndIsRefused) {
    const auto lesson = LessonBuilder{}.Build();

    const auto out =
        lesson.Withdraw(Policy(), Rub(150000), Student(), Student(), lesson.StartsAt() - 48h);

    ASSERT_FALSE(out.HasValue());
    EXPECT_EQ(out.Failure().Code(), "participation_last_one");
}

TEST(LessonParticipation, WithdrawingSomeoneWhoIsNotThereIsRefused) {
    const auto lesson = LessonBuilder{}.Build();

    const auto out =
        lesson.Withdraw(Policy(), Rub(150000), Second(), Tutor(), lesson.StartsAt() - 48h);

    ASSERT_FALSE(out.HasValue());
    EXPECT_EQ(out.Failure().Code(), "participation_not_found");
}

/// Отмена по-прежнему меняет СОСТОЯНИЕ ЗАНЯТИЯ, а не состояние участия: это
/// вторая половина того же различения.
TEST(LessonParticipation, CancellingChangesTheLessonAndNotTheParticipation) {
    const auto lesson = LessonBuilder{}.Build();

    const auto cancelled =
        lesson.CancelByStudent(Policy(), Rub(150000), Student(), lesson.StartsAt() - 48h);

    ASSERT_TRUE(cancelled.HasValue()) << cancelled.Failure().Code();
    EXPECT_EQ(cancelled.Value().lesson.State(), LessonState::kCancelled);
    EXPECT_EQ(cancelled.Value().lesson.Participants().front().State(), ParticipationState::kJoined)
        << "отмена занятия вывела участника: это разные факты";
}

}  // namespace pdr::scheduling
