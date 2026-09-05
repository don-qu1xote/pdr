#include <chrono>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "fakes/fake_clock.hpp"
#include "scheduling/core/cancellation_policy.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/lesson_history.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::FakeClock;
using pdr::testing::Numbered;

constexpr int kLate = 50;
constexpr int kNoShow = 100;
constexpr std::int64_t kPriceMinor = 400000;

core::CurrencyCode Roubles() {
    return core::CurrencyCode::Parse("RUB").value();
}

core::Money Price() {
    return core::Money::FromMinorUnits(kPriceMinor, Roubles());
}

core::Money Nothing() {
    return core::Money::FromMinorUnits(0, Roubles());
}

core::Percent Share(int value) {
    return core::Percent::Compose(value).value();
}

core::PersonId Student() {
    return Numbered<core::PersonId>(20);
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

CancellationPolicy Policy(int free_reschedules = 1) {
    const auto composed =
        CancellationPolicy::Compose(std::chrono::duration_cast<CancellationPolicy::Window>(24h),
                                    Share(kLate),
                                    Share(kNoShow),
                                    free_reschedules);
    EXPECT_TRUE(composed.HasValue());
    return composed.Value();
}

/// Занятие ровно через сутки от начала отсчёта фейковых часов. Все проверки
/// границы двигают ЧАСЫ, а не занятие: так граница остаётся одним числом.
Lesson ALesson() {
    return LessonBuilder{}
        .Id(Numbered<core::LessonId>(100))
        .InTenant(Numbered<core::TenantId>(1))
        .Between(Tutor(), Student())
        .StartingAt(FakeClock::DefaultStart() + 24h)
        .Build();
}

std::vector<LessonHistoryEntry> Moves(int times) {
    std::vector<LessonHistoryEntry> history;
    for (int index = 0; index < times; ++index) {
        history.push_back(LessonHistoryEntry{Numbered<core::TenantId>(1),
                                             Numbered<core::LessonId>(100),
                                             Student(),
                                             LessonAction::kRescheduled,
                                             FakeClock::DefaultStart(),
                                             std::string{}});
    }
    return history;
}

class CancellationTest : public ::testing::Test {
protected:
    FakeClock clock_;
};

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: граница окна, за минуту до и за минуту после.
///
/// Три случая, а не один: правило «не позже чем за сутки» ломается ровно на
/// границе, и ломается молча — обе стороны узнают об этом на споре о деньгах.
TEST_F(CancellationTest, TheFreeWindowIsDecidedOnItsExactBoundary) {
    const auto lesson = ALesson();

    const auto exactly = lesson.CancelByStudent(Policy(), Price(), Student(), clock_.Now());
    ASSERT_TRUE(exactly.HasValue());
    EXPECT_EQ(exactly.Value().outcome.reason, RetentionReason::kInsideFreeWindow)
        << "отмена ровно за сутки удержала деньги: граница не включена в окно";
    EXPECT_EQ(exactly.Value().outcome.retained, Nothing());

    clock_.Advance(-1min);
    const auto earlier = lesson.CancelByStudent(Policy(), Price(), Student(), clock_.Now());
    ASSERT_TRUE(earlier.HasValue());
    EXPECT_EQ(earlier.Value().outcome.reason, RetentionReason::kInsideFreeWindow);
    EXPECT_EQ(earlier.Value().outcome.retained, Nothing());

    clock_.Advance(2min);
    const auto later = lesson.CancelByStudent(Policy(), Price(), Student(), clock_.Now());
    ASSERT_TRUE(later.HasValue());
    EXPECT_EQ(later.Value().outcome.reason, RetentionReason::kLateCancellation)
        << "отмена за минуту до границы прошла как бесплатная";
    EXPECT_EQ(later.Value().outcome.retained,
              core::Money::FromMinorUnits(kPriceMinor * kLate / 100, Roubles()));
}

TEST_F(CancellationTest, TheRetainedAmountIsExactlyWhatThePolicySays) {
    const auto lesson = ALesson();
    clock_.Advance(1min);

    const auto policy =
        CancellationPolicy::Compose(std::chrono::duration_cast<CancellationPolicy::Window>(24h),
                                    Share(30),
                                    Share(kNoShow),
                                    1)
            .Value();

    const auto cancelled = lesson.CancelByStudent(policy, Price(), Student(), clock_.Now());

    ASSERT_TRUE(cancelled.HasValue());
    EXPECT_EQ(cancelled.Value().outcome.retained,
              core::Money::FromMinorUnits(kPriceMinor * 30 / 100, Roubles()));
    EXPECT_EQ(cancelled.Value().outcome.new_state, LessonState::kCancelled);
}

/// ОТМЕНА РЕПЕТИТОРОМ — ВСЕГДА БЕЗ УДЕРЖАНИЯ. Политика при этом самая суровая
/// из возможных, и передать её сюда нечем: параметра нет.
TEST_F(CancellationTest, TheTutorCancelsWithoutRetentionEvenLate) {
    const auto lesson = ALesson();
    clock_.Advance(24h - 1min);

    const auto cancelled = lesson.CancelByTutor(Roubles(), Tutor(), clock_.Now());

    ASSERT_TRUE(cancelled.HasValue());
    EXPECT_EQ(cancelled.Value().outcome.retained, Nothing());
    EXPECT_EQ(cancelled.Value().outcome.reason, RetentionReason::kTutorCancelled);
    EXPECT_EQ(cancelled.Value().record.action, LessonAction::kCancelledByTutor);
}

/// ПЕРЕНОС СОХРАНЯЕТ ЛИЧНОСТЬ ЗАНЯТИЯ: тот же идентификатор, тот же тенант, те
/// же участники. Новое занятие на этом месте разорвало бы связь с оплатой и
/// прогрессом, и восстановить её было бы нечем.
TEST_F(CancellationTest, ReschedulingKeepsTheSameLesson) {
    const auto lesson = ALesson();
    const auto moved =
        lesson.Reschedule(Policy(), Price(), Student(), clock_.Now() + 48h, clock_.Now(), {});

    ASSERT_TRUE(moved.HasValue());
    EXPECT_TRUE(moved.Value().lesson.Id() == lesson.Id()) << "перенос завёл новое занятие";
    EXPECT_TRUE(moved.Value().lesson.Tenant() == lesson.Tenant());
    EXPECT_TRUE(moved.Value().lesson.Participants() == lesson.Participants());
    EXPECT_TRUE(moved.Value().lesson.StartsAt() == clock_.Now() + 48h);
    EXPECT_EQ(moved.Value().lesson.State(), lesson.State());
    EXPECT_EQ(moved.Value().record.action, LessonAction::kRescheduled);
    EXPECT_EQ(moved.Value().record.details,
              "was=" + std::to_string(lesson.StartsAt().UnixMicros()));
}

/// Первый перенос бесплатен, следующий за окном считается поздней отменой.
TEST_F(CancellationTest, TheFirstMoveIsFreeAndTheNextLateOneIsNot) {
    const auto lesson = ALesson();
    clock_.Advance(1min);

    const auto history = Moves(1);
    const auto first =
        lesson.Reschedule(Policy(), Price(), Student(), clock_.Now() + 48h, clock_.Now(), {});
    ASSERT_TRUE(first.HasValue());
    EXPECT_EQ(first.Value().outcome.reason, RetentionReason::kFreeReschedule);
    EXPECT_EQ(first.Value().outcome.retained, Nothing());

    const auto second =
        lesson.Reschedule(Policy(), Price(), Student(), clock_.Now() + 48h, clock_.Now(), history);
    ASSERT_TRUE(second.HasValue());
    EXPECT_EQ(second.Value().outcome.reason, RetentionReason::kLateReschedule);
    EXPECT_EQ(second.Value().outcome.retained,
              core::Money::FromMinorUnits(kPriceMinor * kLate / 100, Roubles()));
}

/// Оговорка к предыдущему: перенос ВНУТРИ окна бесплатен и вторым, и десятым.
/// Иначе платить пришлось бы за то, что и так можно сделать отменой с записью
/// заново.
TEST_F(CancellationTest, AMoveInsideTheWindowIsFreeHoweverManyThereWere) {
    const auto lesson = ALesson();

    const auto moved =
        lesson.Reschedule(Policy(), Price(), Student(), clock_.Now() + 48h, clock_.Now(), Moves(5));

    ASSERT_TRUE(moved.HasValue());
    EXPECT_EQ(moved.Value().outcome.reason, RetentionReason::kFreeReschedule);
    EXPECT_EQ(moved.Value().outcome.retained, Nothing());
}

TEST_F(CancellationTest, TheMoveIsRefusedIntoThePastAndOntoTheSamePlace) {
    const auto lesson = ALesson();

    const auto backwards =
        lesson.Reschedule(Policy(), Price(), Student(), clock_.Now() - 1min, clock_.Now(), {});
    ASSERT_FALSE(backwards.HasValue());
    EXPECT_EQ(backwards.Failure().Code(), "lesson_starts_in_past");

    const auto nowhere =
        lesson.Reschedule(Policy(), Price(), Student(), lesson.StartsAt(), clock_.Now(), {});
    ASSERT_FALSE(nowhere.HasValue());
    EXPECT_EQ(nowhere.Failure().Code(), "lesson_moved_nowhere");
}

TEST_F(CancellationTest, AHeldLessonIsPaidForInFull) {
    const auto lesson = ALesson();

    const auto held = lesson.MarkHeld(Price(), Tutor(), clock_.Now());

    ASSERT_TRUE(held.HasValue());
    EXPECT_EQ(held.Value().outcome.new_state, LessonState::kHeld);
    EXPECT_EQ(held.Value().outcome.retained, Price());
    EXPECT_EQ(held.Value().outcome.reason, RetentionReason::kLessonHeld);
}

TEST_F(CancellationTest, NoShowRetainsItsOwnShareAndNotTheLateOne) {
    const auto lesson = ALesson();
    const auto policy =
        CancellationPolicy::Compose(
            std::chrono::duration_cast<CancellationPolicy::Window>(24h), Share(kLate), Share(80), 1)
            .Value();

    const auto missed = lesson.MarkNoShow(policy, Price(), Tutor(), clock_.Now());

    ASSERT_TRUE(missed.HasValue());
    EXPECT_EQ(missed.Value().outcome.new_state, LessonState::kNoShow);
    EXPECT_EQ(missed.Value().outcome.retained,
              core::Money::FromMinorUnits(kPriceMinor * 80 / 100, Roubles()));
    EXPECT_EQ(missed.Value().outcome.reason, RetentionReason::kNoShow);
}

/// НЕДОПУСТИМЫЕ ПЕРЕХОДЫ ОТКЛОНЕНЫ — все, а не выборочно. Проведённое занятие
/// не отменяется и не переносится задним числом, отменённое не проводится.
TEST_F(CancellationTest, NothingHappensToALessonThatIsAlreadyOver) {
    const auto held = ALesson().MarkHeld(Price(), Tutor(), clock_.Now()).Value().lesson;

    for (const auto& refused :
         {held.CancelByStudent(Policy(), Price(), Student(), clock_.Now()),
          held.CancelByTutor(Roubles(), Tutor(), clock_.Now()),
          held.Reschedule(Policy(), Price(), Student(), clock_.Now() + 48h, clock_.Now(), {}),
          held.MarkHeld(Price(), Tutor(), clock_.Now()),
          held.MarkNoShow(Policy(), Price(), Tutor(), clock_.Now())}) {
        ASSERT_FALSE(refused.HasValue());
        EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kConflict);
        EXPECT_EQ(refused.Failure().Code(), "lesson_transition_not_allowed");
    }
}

TEST_F(CancellationTest, ACancelledLessonIsNotHeldAfterwards) {
    const auto cancelled = ALesson().CancelByTutor(Roubles(), Tutor(), clock_.Now()).Value().lesson;

    const auto refused = cancelled.MarkHeld(Price(), Tutor(), clock_.Now());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "lesson_transition_not_allowed");
}

TEST_F(CancellationTest, EveryOperationWritesItsOwnLineOfHistory) {
    const auto lesson = ALesson();

    const auto cancelled = lesson.CancelByStudent(Policy(), Price(), Student(), clock_.Now());

    ASSERT_TRUE(cancelled.HasValue());
    const auto& record = cancelled.Value().record;
    EXPECT_TRUE(record.lesson == lesson.Id());
    EXPECT_TRUE(record.tenant == lesson.Tenant());
    EXPECT_TRUE(record.actor == Student());
    EXPECT_EQ(record.action, LessonAction::kCancelledByStudent);
    EXPECT_TRUE(record.at == clock_.Now()) << "момент истории взят не у часов";
}

TEST_F(CancellationTest, APolicyOutsideItsLimitsIsRefused) {
    EXPECT_FALSE(core::Percent::Compose(101).has_value());
    EXPECT_FALSE(core::Percent::Compose(-1).has_value());

    const auto backwards = CancellationPolicy::Compose(
        std::chrono::duration_cast<CancellationPolicy::Window>(-1h), Share(0), Share(0), 0);
    ASSERT_FALSE(backwards.HasValue());
    EXPECT_EQ(backwards.Failure().Code(), "cancellation_window_negative");

    const auto negative = CancellationPolicy::Compose(
        std::chrono::duration_cast<CancellationPolicy::Window>(24h), Share(0), Share(0), -1);
    ASSERT_FALSE(negative.HasValue());
    EXPECT_EQ(negative.Failure().Code(), "cancellation_free_moves_negative");
}

}  // namespace
}  // namespace pdr::scheduling
