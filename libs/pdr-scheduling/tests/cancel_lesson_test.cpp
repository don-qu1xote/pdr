#include "scheduling/application/cancel_lesson.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_scheduling.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeLessonHistory;
using pdr::scheduling::testing::FakeLessons;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::FakeClock;
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

core::LessonId Which() {
    return Numbered<core::LessonId>(100);
}

/// Политика, которую отдаёт порт. Значений в ней нет ни одного «на всякий
/// случай»: сценарий берёт их отсюда, а не из своей головы.
class FakePolicies final : public ports::CancellationPolicies {
public:
    explicit FakePolicies(int late_percent) : late_percent_{late_percent} {}

    core::Result<CancellationPolicy> Of(const core::TenantId&) const override {
        ++asked;
        return CancellationPolicy::Compose(
            std::chrono::duration_cast<CancellationPolicy::Window>(24h),
            core::Percent::Compose(late_percent_).value(),
            core::Percent::Compose(100).value(),
            1);
    }

    mutable int asked{0};

private:
    int late_percent_;
};

class CancelLessonTest : public ::testing::Test {
protected:
    CancelLessonTest() {
        const auto lesson = LessonBuilder{}
                                .Id(Which())
                                .InTenant(Tenant())
                                .Between(Tutor(), Student())
                                .StartingAt(clock_.Now() + 24h)
                                .Build();
        EXPECT_TRUE(lessons_.Save(lesson).HasValue());
    }

    CancelLesson Cancelling() {
        return CancelLesson{lessons_, history_, policies_, clock_, bus_};
    }

    CancelLesson::Request Request(CancelledBy by) const {
        return CancelLesson::Request{
            Tenant(), by == CancelledBy::kTutor ? Tutor() : Student(), Which(), by, Price()};
    }

    std::vector<events::scheduling::LessonCancelled> Heard() {
        std::vector<events::scheduling::LessonCancelled> heard;
        bus_.Subscribe<events::scheduling::LessonCancelled>(
            [&heard](const events::scheduling::LessonCancelled& event) { heard.push_back(event); });
        return heard;
    }

    FakeClock clock_;
    FakeLessons lessons_;
    FakeLessonHistory history_;
    FakePolicies policies_{50};
    events::InMemoryBus bus_;
};

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: событие уходит с посчитанной суммой, и ни одного
/// обращения к деньгам по дороге нет — их и взять неоткуда, порта биллинга у
/// сценария не существует.
TEST_F(CancelLessonTest, TheEventCarriesTheCountedRetentionAndNobodyAsksBilling) {
    std::vector<events::scheduling::LessonCancelled> heard;
    bus_.Subscribe<events::scheduling::LessonCancelled>(
        [&heard](const events::scheduling::LessonCancelled& event) { heard.push_back(event); });

    clock_.Advance(1min);
    const auto cancelled = Cancelling().Execute(Request(CancelledBy::kStudent));

    ASSERT_TRUE(cancelled.HasValue());
    ASSERT_EQ(heard.size(), 1U);
    EXPECT_TRUE(heard.front().lesson == Which());
    EXPECT_TRUE(heard.front().actor == Student());
    EXPECT_EQ(heard.front().by, CancelledBy::kStudent);
    EXPECT_EQ(heard.front().retained, core::Money::FromMinorUnits(kPriceMinor / 2, Roubles()));
    EXPECT_EQ(heard.front().reason, RetentionReason::kLateCancellation);
    EXPECT_TRUE(heard.front().envelope.tenant == Tenant());
    EXPECT_TRUE(heard.front().envelope.occurred_at == clock_.Now());
}

TEST_F(CancelLessonTest, TheLessonKeepsItsIdentityAndGetsItsLineOfHistory) {
    ASSERT_TRUE(Cancelling().Execute(Request(CancelledBy::kStudent)).HasValue());

    const auto kept = lessons_.Find(Tenant(), Which());
    ASSERT_TRUE(kept.has_value());
    EXPECT_EQ(kept->State(), LessonState::kCancelled);
    EXPECT_EQ(lessons_.Kept().size(), 1U) << "отмена завела второе занятие";

    const auto history = history_.Of(Tenant(), Which());
    ASSERT_EQ(history.size(), 1U);
    EXPECT_EQ(history.front().action, LessonAction::kCancelledByStudent);
    EXPECT_TRUE(history.front().actor == Student());
}

/// Отмена репетитором не спрашивает политику ВООБЩЕ: удерживать нечего, и
/// спрашивать не о чем.
TEST_F(CancelLessonTest, TheTutorsCancellationDoesNotEvenAskForThePolicy) {
    clock_.Advance(23h);

    const auto cancelled = Cancelling().Execute(Request(CancelledBy::kTutor));

    ASSERT_TRUE(cancelled.HasValue());
    EXPECT_EQ(cancelled.Value().retained, core::Money::FromMinorUnits(0, Roubles()));
    EXPECT_EQ(cancelled.Value().reason, RetentionReason::kTutorCancelled);
    EXPECT_EQ(policies_.asked, 0) << "политику спросили там, где она ничего не решает";
}

TEST_F(CancelLessonTest, WhatWasNeverBookedIsNotCancelled) {
    const auto refused = Cancelling().Execute(CancelLesson::Request{
        Tenant(), Student(), Numbered<core::LessonId>(999), CancelledBy::kStudent, Price()});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "lesson_not_found");
    EXPECT_EQ(bus_.Published(), 0U);
}

TEST_F(CancelLessonTest, ARefusedCancellationPublishesNothing) {
    ASSERT_TRUE(Cancelling().Execute(Request(CancelledBy::kStudent)).HasValue());

    const auto again = Cancelling().Execute(Request(CancelledBy::kStudent));

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "lesson_transition_not_allowed");
    EXPECT_EQ(bus_.Published(), 1U) << "отклонённая отмена всё-таки опубликовала событие";
    EXPECT_EQ(history_.Of(Tenant(), Which()).size(), 1U);
}

}  // namespace
}  // namespace pdr::scheduling
