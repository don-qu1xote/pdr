#include "identity/core/login_throttle.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "fakes/fake_clock.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;

core::Instant::Duration Of(std::chrono::minutes minutes) {
    return std::chrono::duration_cast<core::Instant::Duration>(minutes);
}

const auto kWindow = Of(15min);

TEST(ThrottleLimits, AddressIsNeverStricterThanAccount) {
    const auto refused = ThrottleLimits::Compose(kWindow, 10, 5);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "throttle_address_below_account")
        << "за одним адресом сидит целый класс, за одной записью — один человек";
}

TEST(ThrottleLimits, ZeroThresholdLocksEveryoneOut) {
    const auto refused = ThrottleLimits::Compose(kWindow, 0, 10);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "throttle_limit_too_low");
}

TEST(ThrottleLimits, WindowOfZeroLengthCountsNothing) {
    const auto refused = ThrottleLimits::Compose(core::Instant::Duration::zero(), 10, 50);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "throttle_window_not_positive");
}

TEST(ThrottleLimits, CountersAreSeparateAndAddressIsRoomier) {
    const auto limits = ThrottleLimits::Compose(kWindow, 10, 50).Value();

    EXPECT_EQ(limits.For(AttemptSubject::kAccount), 10U);
    EXPECT_EQ(limits.For(AttemptSubject::kAddress), 50U);
}

TEST(AttemptSubject, NamesAreTheWordsTheDatabaseKnows) {
    for (const auto subject : {AttemptSubject::kAccount, AttemptSubject::kAddress}) {
        const auto parsed = ParseAttemptSubject(Name(subject));

        ASSERT_TRUE(parsed.has_value());
        EXPECT_EQ(*parsed, subject);
    }
    EXPECT_FALSE(ParseAttemptSubject("device").has_value());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ, первая половина: превышение порога запирает.
TEST(AttemptWindow, ThreeMissesInAWindowLockTheDoor) {
    const auto start = testing::FakeClock::DefaultStart();
    auto window = AttemptWindow::Restore(start, 0);

    for (int miss = 1; miss <= 3; ++miss) {
        window = window.Registered(start + Of(std::chrono::minutes{miss}), kWindow);
    }

    EXPECT_EQ(window.Attempts(), 3U);
    EXPECT_TRUE(window.IsBlockedAt(start + Of(4min), kWindow, 3));
    EXPECT_FALSE(window.IsBlockedAt(start + Of(4min), kWindow, 4))
        << "при пороге в четыре попытки три ещё не запирают";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ, вторая половина: запрет снимается сам по
/// истечении окна, без чьего-либо участия.
TEST(AttemptWindow, TheDoorOpensWhenTheWindowRunsOut) {
    const auto start = testing::FakeClock::DefaultStart();
    auto window = AttemptWindow::Restore(start, 5);

    EXPECT_TRUE(window.IsBlockedAt(start + Of(14min), kWindow, 3));
    EXPECT_FALSE(window.IsBlockedAt(start + Of(15min), kWindow, 3))
        << "окно кончилось — запрет снялся, и никто для этого ничего не делал";
}

/// Окно скользит по ПЕРВОЙ попытке, а не по последней: иначе попытка раз в
/// минуту продлевала бы запрет вечно, и человек, вспомнивший пароль, не смог
/// бы войти никогда.
TEST(AttemptWindow, TheWindowDoesNotSlideOnEveryMiss) {
    const auto start = testing::FakeClock::DefaultStart();

    const auto second = AttemptWindow::Restore(start, 1).Registered(start + Of(10min), kWindow);

    EXPECT_EQ(second.StartedAt(), start) << "начало окна сдвинулось вместе с попыткой";
    EXPECT_EQ(second.Attempts(), 2U);
}

TEST(AttemptWindow, MissAfterTheWindowStartsCountingAnew) {
    const auto start = testing::FakeClock::DefaultStart();

    const auto fresh = AttemptWindow::Restore(start, 9).Registered(start + Of(20min), kWindow);

    EXPECT_EQ(fresh.StartedAt(), start + Of(20min));
    EXPECT_EQ(fresh.Attempts(), 1U) << "счёт не начался заново после истёкшего окна";
}

TEST(AttemptWindow, NothingSeenLocksNoOne) {
    const auto empty = AttemptWindow::Restore(core::Instant::FromUnixMicros(0), 0);

    EXPECT_FALSE(empty.IsBlockedAt(testing::FakeClock::DefaultStart(), kWindow, 1));
}

}  // namespace
}  // namespace pdr::identity
