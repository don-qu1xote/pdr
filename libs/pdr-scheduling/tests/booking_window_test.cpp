#include "scheduling/core/booking_window.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "fakes/fake_clock.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using Notice = BookingWindows::Notice;

BookingWindows Windows(std::optional<Notice> book,
                       std::optional<Notice> horizon = std::nullopt,
                       std::optional<Notice> reschedule = std::nullopt,
                       std::optional<Notice> cancel = std::nullopt) {
    const auto composed = BookingWindows::Compose(book, horizon, reschedule, cancel);
    EXPECT_TRUE(composed.HasValue()) << "образцовые окна не собрались";
    return composed.Value();
}

class BookingWindowTest : public ::testing::Test {
protected:
    core::Instant Now() const {
        return clock_.Now();
    }

    core::Instant In(core::Instant::Duration ahead) const {
        return clock_.Now() + ahead;
    }

    /// Просит ученик: именно он и ограничен окнами.
    core::Result<void> Asked(const BookingWindows& windows,
                             BookingAction action,
                             core::Instant starts_at) const {
        return Allows(windows, action, BookingSide::kIncoming, starts_at, Now());
    }

    pdr::testing::FakeClock clock_;
};

}  // namespace

/// ГРАНИЦА ОКНА ЗАПИСИ, все три её стороны. Ровно на границе — можно: «за два
/// часа» значит «за два часа», и человеку, нажавшему кнопку секунда в секунду,
/// не объясняют, что имелось в виду «за два часа и одну секунду».
TEST_F(BookingWindowTest, TheBookingNoticeHoldsExactlyWhereItSays) {
    const auto windows = Windows(2h);

    EXPECT_TRUE(Asked(windows, BookingAction::kBook, In(2h)).HasValue())
        << "запись ровно за два часа отклонена";
    EXPECT_TRUE(Asked(windows, BookingAction::kBook, In(2h + 1min)).HasValue());

    const auto late = Asked(windows, BookingAction::kBook, In(2h - 1min));
    ASSERT_FALSE(late.HasValue()) << "записались позже, чем окно разрешает";
    EXPECT_EQ(late.Failure().Code(), "booking_too_late");
}

/// ГРАНИЦА ГОРИЗОНТА. Считается вперёд от «сейчас», а не назад от занятия, — и
/// ровно на месяц вперёд записаться можно.
TEST_F(BookingWindowTest, TheHorizonHoldsExactlyWhereItSays) {
    const auto windows = Windows(std::nullopt, 30 * 24h);

    EXPECT_TRUE(Asked(windows, BookingAction::kBook, In(30 * 24h)).HasValue())
        << "запись ровно на границе горизонта отклонена";
    EXPECT_TRUE(Asked(windows, BookingAction::kBook, In(30 * 24h - 1min)).HasValue());

    const auto far = Asked(windows, BookingAction::kBook, In(30 * 24h + 1min));
    ASSERT_FALSE(far.HasValue()) << "записались дальше открытого расписания";
    EXPECT_EQ(far.Failure().Code(), "booking_too_far");
}

TEST_F(BookingWindowTest, TheRescheduleNoticeHoldsExactlyWhereItSays) {
    const auto windows = Windows(std::nullopt, std::nullopt, 4h);

    EXPECT_TRUE(Asked(windows, BookingAction::kReschedule, In(4h)).HasValue());

    const auto late = Asked(windows, BookingAction::kReschedule, In(4h - 1min));
    ASSERT_FALSE(late.HasValue());
    EXPECT_EQ(late.Failure().Code(), "reschedule_too_late");
}

TEST_F(BookingWindowTest, TheCancelNoticeHoldsExactlyWhereItSays) {
    const auto windows = Windows(std::nullopt, std::nullopt, std::nullopt, 1h);

    EXPECT_TRUE(Asked(windows, BookingAction::kCancel, In(1h)).HasValue());

    const auto late = Asked(windows, BookingAction::kCancel, In(1h - 1min));
    ASSERT_FALSE(late.HasValue());
    EXPECT_EQ(late.Failure().Code(), "cancel_too_late");
}

/// ОКНА У ТРЁХ ДЕЙСТВИЙ РАЗНЫЕ, и одно за другое не отвечает. Иначе «перенести
/// нельзя» превращалось бы в «записаться нельзя» на том же занятии.
TEST_F(BookingWindowTest, EachActionAnswersForItselfAlone) {
    const auto windows = Windows(24h, std::nullopt, 4h, std::nullopt);

    EXPECT_FALSE(Asked(windows, BookingAction::kBook, In(5h)).HasValue());
    EXPECT_TRUE(Asked(windows, BookingAction::kReschedule, In(5h)).HasValue());
    EXPECT_TRUE(Asked(windows, BookingAction::kCancel, In(1min)).HasValue())
        << "окно отмены пустое, а отмену всё равно отклонили";
}

/// «БЕЗ ОГРАНИЧЕНИЯ» — ЭТО НЕ НОЛЬ. Пустое окно разрешает всё; нулевое
/// разрешает вплоть до самого начала и отказывает после него.
TEST_F(BookingWindowTest, NothingIsNotZero) {
    const auto nothing = BookingWindows::Anything();
    const auto zero = Windows(Notice::zero());

    EXPECT_TRUE(Asked(nothing, BookingAction::kBook, In(1s)).HasValue());
    EXPECT_TRUE(Asked(nothing, BookingAction::kBook, Now() - 1h).HasValue())
        << "пустое окно должно молчать обо всём, включая прошлое";

    EXPECT_TRUE(Asked(zero, BookingAction::kBook, Now()).HasValue());
    EXPECT_FALSE(Asked(zero, BookingAction::kBook, Now() - 1min).HasValue());
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: репетитор своими окнами не ограничен.
/// Договорился голосом и ставит занятие через минуту — это его расписание.
TEST_F(BookingWindowTest, TheOwnerOfTheScheduleIsNotBoundByIt) {
    const auto windows = Windows(24h, 7 * 24h, 24h, 24h);

    for (const auto action : kEveryBookingAction) {
        EXPECT_TRUE(
            Allows(windows, action, BookingSide::kScheduleOwner, In(1min), Now()).HasValue())
            << "репетитору отказали в его собственном расписании: " << Name(action);
    }
    EXPECT_TRUE(
        Allows(windows, BookingAction::kBook, BookingSide::kScheduleOwner, In(365 * 24h), Now())
            .HasValue())
        << "репетитора не пустили дальше собственного горизонта";
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: ослабление работает. У постоянного ученика
/// третий год другие права, чем у пришедшего вчера.
TEST_F(BookingWindowTest, ARelaxationForOneStudentIsAccepted) {
    const auto base = Windows(24h, 30 * 24h, 24h, 24h);
    const auto softer = Windows(1h, 90 * 24h, 1h, std::nullopt);

    const auto relaxed = Relax(base, softer);

    ASSERT_TRUE(relaxed.HasValue()) << relaxed.Failure().Code();
    EXPECT_EQ(relaxed.Value(), softer);
    EXPECT_TRUE(Allows(relaxed.Value(), BookingAction::kBook, BookingSide::kIncoming, In(2h), Now())
                    .HasValue())
        << "послабление выдано, а запись за два часа всё равно отклонена";
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: ужесточение отклоняется. Персональная
/// строгость — правило, которого ученик не увидит заранее и не оспорит.
TEST_F(BookingWindowTest, ATighteningForOneStudentIsRefused) {
    const auto base = Windows(24h, 30 * 24h, 24h, 24h);

    for (const auto& stricter : {Windows(48h, 30 * 24h, 24h, 24h),
                                 Windows(24h, 7 * 24h, 24h, 24h),
                                 Windows(24h, 30 * 24h, 48h, 24h),
                                 Windows(24h, 30 * 24h, 24h, 48h)}) {
        const auto refused = Relax(base, stricter);
        ASSERT_FALSE(refused.HasValue()) << "ужесточение прошло";
        EXPECT_EQ(refused.Failure().Code(), "booking_window_tightened");
    }
}

/// Пустое окно — самое слабое, какое бывает. Поставить на его место срок —
/// ужесточение, сколько бы мягким этот срок ни выглядел.
TEST_F(BookingWindowTest, PuttingALimitWhereThereWasNoneIsATightening) {
    const auto base = Windows(std::nullopt, std::nullopt, std::nullopt, std::nullopt);

    const auto refused = Relax(base, Windows(1min));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "booking_window_tightened");
}

/// Одинаковые окна — не ужесточение: послабление, ничего не меняющее, отклонять
/// не за что.
TEST_F(BookingWindowTest, TheSameWindowsAreNotATightening) {
    const auto base = Windows(24h, 30 * 24h, 24h, 24h);

    EXPECT_TRUE(Relax(base, base).HasValue());
}

/// Послабления нет — действуют окна практики. Это обычный случай: у большинства
/// пар послаблений нет и никогда не будет.
TEST_F(BookingWindowTest, WithoutARelaxationThePracticeWindowsStand) {
    const auto base = Windows(24h, 30 * 24h);

    const auto relaxed = Relax(base, std::nullopt);

    ASSERT_TRUE(relaxed.HasValue());
    EXPECT_EQ(relaxed.Value(), base);
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: недоступное не показывается как отказ — оно
/// не показывается вовсе. Слот, который видно и на который нельзя записаться, —
/// это отказ, отложенный до нажатия.
TEST_F(BookingWindowTest, SlotsOutsideTheWindowsAreNotOffered) {
    const auto windows = Windows(24h, 7 * 24h);
    const std::vector<core::Instant> slots{
        In(1h), In(23h), In(24h), In(48h), In(7 * 24h), In(8 * 24h)};

    const auto offered = Offered(slots, windows, BookingSide::kIncoming, Now());

    EXPECT_EQ(offered, (std::vector<core::Instant>{In(24h), In(48h), In(7 * 24h)}));
}

/// Репетитору видно всё своё расписание: фильтр — про входящих.
TEST_F(BookingWindowTest, TheOwnerSeesEverySlotOfHisOwnSchedule) {
    const auto windows = Windows(24h, 7 * 24h);
    const std::vector<core::Instant> slots{In(1h), In(48h), In(30 * 24h)};

    EXPECT_EQ(Offered(slots, windows, BookingSide::kScheduleOwner, Now()), slots);
}

TEST_F(BookingWindowTest, AWindowCountedBackwardsIsRefused) {
    const auto refused = BookingWindows::Compose(-1h, std::nullopt, std::nullopt, std::nullopt);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "booking_window_negative");
}

TEST_F(BookingWindowTest, EveryActionHasAName) {
    for (const auto action : kEveryBookingAction) {
        EXPECT_FALSE(Name(action).empty());
    }
}

}  // namespace pdr::scheduling
