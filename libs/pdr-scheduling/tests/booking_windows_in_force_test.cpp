#include <chrono>
#include <optional>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_booking_windows.hpp"
#include "fakes/fake_clock.hpp"
#include "scheduling/application/relax_booking_windows.hpp"
#include "scheduling/application/windows_in_force.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeWindowDefaults;
using pdr::scheduling::testing::FakeWindowRelief;
using pdr::testing::Numbered;

BookingWindows Windows(std::optional<BookingWindows::Notice> book,
                       std::optional<BookingWindows::Notice> horizon = std::nullopt,
                       std::optional<BookingWindows::Notice> reschedule = std::nullopt,
                       std::optional<BookingWindows::Notice> cancel = std::nullopt) {
    const auto composed = BookingWindows::Compose(book, horizon, reschedule, cancel);
    EXPECT_TRUE(composed.HasValue());
    return composed.Value();
}

class WindowsInForceTest : public ::testing::Test {
protected:
    RelaxBookingWindows Relaxing() {
        return RelaxBookingWindows{defaults_, relief_, clock_};
    }

    RelaxBookingWindows::Request Request(BookingWindows windows) const {
        return RelaxBookingWindows::Request{tenant_, tutor_, student_, tutor_, std::move(windows)};
    }

    pdr::testing::FakeClock clock_;
    FakeWindowDefaults defaults_{Windows(24h, 30 * 24h, 24h, 24h)};
    FakeWindowRelief relief_;
    WindowsInForce in_force_{defaults_, relief_};

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId tutor_{Numbered<core::PersonId>(10)};
    core::PersonId student_{Numbered<core::PersonId>(20)};
    core::PersonId stranger_{Numbered<core::PersonId>(30)};
};

}  // namespace

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: всё работает без единой настройки.
///
/// Ни одна строка послаблений не заведена, а окна действуют — те, что пришли из
/// умолчаний. Человек попадает в настройки, только когда сам захочет.
TEST_F(WindowsInForceTest, TheDefaultsWorkWithoutASingleSetting) {
    const auto windows = in_force_.For(tenant_, tutor_, student_);

    ASSERT_TRUE(windows.HasValue()) << windows.Failure().Code();
    EXPECT_EQ(windows.Value().MinNoticeBook(), 24h);
    EXPECT_TRUE(relief_.Given().empty()) << "настройка завелась сама";
}

TEST_F(WindowsInForceTest, ARelaxationTakesEffectForThatPairOnly) {
    ASSERT_TRUE(Relaxing().Execute(Request(Windows(1h, 30 * 24h, 1h, 24h))).HasValue());

    const auto theirs = in_force_.For(tenant_, tutor_, student_);
    ASSERT_TRUE(theirs.HasValue());
    EXPECT_EQ(theirs.Value().MinNoticeBook(), 1h);

    const auto others = in_force_.For(tenant_, tutor_, stranger_);
    ASSERT_TRUE(others.HasValue());
    EXPECT_EQ(others.Value().MinNoticeBook(), 24h) << "послабление одному ученику досталось всем";
}

/// Послабление принадлежит ПАРЕ: у того же ученика с другим репетитором прав
/// нет никаких.
TEST_F(WindowsInForceTest, ARelaxationDoesNotFollowTheStudentToAnotherTutor) {
    ASSERT_TRUE(Relaxing().Execute(Request(Windows(1h, 30 * 24h, 1h, 24h))).HasValue());

    const auto elsewhere = in_force_.For(tenant_, stranger_, student_);

    ASSERT_TRUE(elsewhere.HasValue());
    EXPECT_EQ(elsewhere.Value().MinNoticeBook(), 24h);
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: ужесточение отклоняется — и отклоняется до
/// записи, а не после. Строки в хранилище не появляется вовсе.
TEST_F(WindowsInForceTest, ATighteningIsRefusedAndNothingIsWrittenDown) {
    const auto refused = Relaxing().Execute(Request(Windows(48h, 30 * 24h, 24h, 24h)));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "booking_window_tightened");
    EXPECT_TRUE(relief_.Given().empty()) << "отклонённое послабление всё-таки записали";
}

TEST_F(WindowsInForceTest, ASecondRelaxationReplacesTheFirst) {
    ASSERT_TRUE(Relaxing().Execute(Request(Windows(12h, 30 * 24h, 24h, 24h))).HasValue());
    ASSERT_TRUE(Relaxing().Execute(Request(Windows(1h, 30 * 24h, 24h, 24h))).HasValue());

    ASSERT_EQ(relief_.Given().size(), 1U) << "у пары завелось два послабления сразу";
    EXPECT_EQ(in_force_.For(tenant_, tutor_, student_).Value().MinNoticeBook(), 1h);
}

/// Кто и когда выдал послабление — в строке, а не в памяти участников: «мне
/// разрешили» через полгода превращается в спор.
TEST_F(WindowsInForceTest, TheRelaxationRemembersWhoGaveIt) {
    ASSERT_TRUE(Relaxing().Execute(Request(Windows(1h, 30 * 24h, 24h, 24h))).HasValue());

    ASSERT_EQ(relief_.Given().size(), 1U);
    EXPECT_TRUE(relief_.Given().front().granted_by == tutor_);
    EXPECT_TRUE(relief_.Given().front().granted_at == clock_.Now());
}

/// Негодная настройка не превращается в «окон нет»: сценарий отказывает, а не
/// пускает всех подряд.
TEST_F(WindowsInForceTest, ABrokenSettingRefusesInsteadOfOpeningEverything) {
    defaults_.Break();

    const auto refused = in_force_.For(tenant_, tutor_, student_);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "booking_window_negative");
}

}  // namespace pdr::scheduling
