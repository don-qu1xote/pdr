#include "notifications/core/retry.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "fakes/fake_clock.hpp"

namespace pdr::notifications {
namespace {

using namespace std::chrono_literals;

RetryPolicy Policy(int max_attempts = 3, core::Instant::Duration first = 1min) {
    const auto composed = RetryPolicy::Compose(max_attempts, first, 30s);
    EXPECT_TRUE(composed.HasValue()) << "образцовая политика не собралась";
    return composed.Value();
}

class RetryPolicyTest : public ::testing::Test {
protected:
    pdr::testing::FakeClock clock_;
};

TEST_F(RetryPolicyTest, DelayDoublesWithEveryFailure) {
    const auto policy = Policy(9);
    const auto now = clock_.Now();

    EXPECT_TRUE(*policy.Again(1, now) == now + 1min);
    EXPECT_TRUE(*policy.Again(2, now) == now + 2min);
    EXPECT_TRUE(*policy.Again(3, now) == now + 4min);
    EXPECT_TRUE(*policy.Again(4, now) == now + 8min);
}

/// ГРАНИЦА ПРЕДЕЛА, и обе её стороны. Последняя попытка ещё повторяется,
/// следующая — уже нет: «после какой именно неудачи мы сдались» — ровно тот
/// вопрос, на который потом отвечают человеку.
TEST_F(RetryPolicyTest, TheLimitIsExactlyWhereItSays) {
    const auto policy = Policy(3);
    const auto now = clock_.Now();

    EXPECT_TRUE(policy.Again(2, now).has_value()) << "сдались раньше предела";
    EXPECT_FALSE(policy.Again(3, now).has_value()) << "предел не остановил повторы";
    EXPECT_FALSE(policy.Again(4, now).has_value());
}

/// Задержка не растёт бесконечно: письмо, доставленное через неделю, — уже не
/// письмо, а недоумение.
TEST_F(RetryPolicyTest, TheDelayHasACeiling) {
    const auto policy = Policy(20);
    const auto now = clock_.Now();

    EXPECT_TRUE(*policy.Again(19, now) == now + RetryPolicy::kLongestDelay);
    EXPECT_TRUE(*policy.Again(19, now) <= now + 24h) << "ждём больше суток между попытками";
}

/// СРОК ПОПЫТКИ — ТО, ЧТО ДЕЛАЕТ ЗАХВАТ ПЕРЕЖИВАЮЩИМ ПАДЕНИЕ. Умерший на
/// отправке воркер не держит строку вечно: она снова станет должной, когда срок
/// выйдет.
TEST_F(RetryPolicyTest, AClaimIsHeldOnlyForTheAttempt) {
    const auto policy = Policy();

    EXPECT_TRUE(policy.Deadline(clock_.Now()) == clock_.Now() + 30s);
    EXPECT_TRUE(policy.Deadline(clock_.Now()) > clock_.Now()) << "захват истекает раньше, чем взят";
}

TEST_F(RetryPolicyTest, ASettingWithoutASingleAttemptIsRefused) {
    const auto refused = RetryPolicy::Compose(0, 1min, 30s);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "retry_attempts_not_positive");
}

TEST_F(RetryPolicyTest, ARepeatWithoutADelayIsRefused) {
    const auto refused = RetryPolicy::Compose(3, 0s, 30s);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "retry_delay_not_positive");
}

TEST_F(RetryPolicyTest, AnAttemptWithoutTimeIsRefused) {
    const auto refused = RetryPolicy::Compose(3, 1min, 0s);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "retry_attempt_not_positive");
}

}  // namespace
}  // namespace pdr::notifications
