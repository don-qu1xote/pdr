#include "notifications/application/deliver_domain_events.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "core/money.hpp"
#include "events/identity/capabilities_widened.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/in_memory_bus.hpp"
#include "events/notifications/reminder_before_return.hpp"
#include "events/notifications/reminder_day_before.hpp"
#include "events/notifications/reminder_hour_before.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"
#include "events/scheduling/time_off_applied.hpp"
#include "events/scheduling/time_off_declared.hpp"
#include "fakes/fake_clock.hpp"

namespace pdr::notifications {
namespace {

using namespace std::chrono_literals;
using pdr::events::scheduling::CancelledBy;
using pdr::events::scheduling::RetentionReason;
using pdr::events::scheduling::TimeOffDecision;
using pdr::testing::Numbered;

/// Очередь фейком — со ВСЕМИ повадками настоящей, кроме базы.
///
/// Повторный ключ не кладёт второй строки, а двигает срок у первой: без этого
/// «перенос двигает напоминание» проверялся бы на фейке, который так не умеет,
/// и зелёный тест ничего не значил бы.
class FakeOutbox final : public ports::OutboxRepository {
public:
    struct Queued final {
        Delivery delivery;
        core::Instant due_at;
    };

    void Enqueue(const Delivery& delivery, core::Instant due_at) override {
        for (auto& already : queued_) {
            if (already.delivery.DedupKey() == delivery.DedupKey()) {
                already = Queued{delivery, due_at};
                return;
            }
        }
        queued_.push_back(Queued{delivery, due_at});
    }

    void Withdraw(const core::TenantId& tenant, const std::string& dedup_key) override {
        withdrawn_.push_back(dedup_key);
        std::erase_if(queued_, [&](const Queued& item) {
            return item.delivery.Tenant() == tenant && item.delivery.DedupKey() == dedup_key;
        });
    }

    const std::vector<Queued>& Rows() const noexcept {
        return queued_;
    }

    const std::vector<std::string>& Withdrawn() const noexcept {
        return withdrawn_;
    }

    std::optional<Queued> WithReason(std::string_view reason) const {
        for (const auto& item : queued_) {
            if (item.delivery.Reason() == reason) {
                return item;
            }
        }
        return std::nullopt;
    }

    std::size_t Counted(std::string_view reason) const {
        return static_cast<std::size_t>(
            std::count_if(queued_.begin(), queued_.end(), [&](const Queued& item) {
                return item.delivery.Reason() == reason;
            }));
    }

private:
    std::vector<Queued> queued_;
    std::vector<std::string> withdrawn_;
};

class DeliverDomainEventsTest : public ::testing::Test {
protected:
    pdr::events::Envelope Envelope() const {
        return {tenant_, clock_.Now()};
    }

    pdr::testing::FakeClock clock_;
    pdr::events::InMemoryBus bus_;
    FakeOutbox outbox_;

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId guardian_{Numbered<core::PersonId>(10)};
    core::PersonId student_{Numbered<core::PersonId>(20)};
    core::PersonId tutor_{Numbered<core::PersonId>(30)};
    core::CurrencyCode rubles_{*core::CurrencyCode::Parse("RUB")};
};

TEST_F(DeliverDomainEventsTest, RevokedGuardianshipTurnsIntoALetter) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});

    ASSERT_EQ(outbox_.Rows().size(), 1U);
    const auto& queued = outbox_.Rows().front().delivery;
    EXPECT_TRUE(queued.Recipient() == guardian_);
    EXPECT_EQ(queued.DeliveryChannel(), Channel::kEmail);
    EXPECT_EQ(queued.Reason(), "identity.guardianship_revoked");
    EXPECT_TRUE(queued.CreatedAt() == clock_.Now());
    EXPECT_TRUE(queued.Tenant() == tenant_);
    EXPECT_FALSE(queued.DedupKey().empty()) << "письмо без ключа намерения уйдёт дважды";
}

TEST_F(DeliverDomainEventsTest, BookedLessonNotifiesBothSides) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), Numbered<core::LessonId>(100), tutor_, student_, clock_.Now() + 48h});

    EXPECT_EQ(outbox_.Counted("scheduling.lesson_booked"), 2U);
    EXPECT_TRUE(outbox_.Rows()[0].delivery.Recipient() == student_);
    EXPECT_EQ(outbox_.Rows()[0].delivery.DeliveryChannel(), Channel::kPush);
}

TEST_F(DeliverDomainEventsTest, NobodySubscribedIsNotAFailure) {
    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});

    EXPECT_TRUE(outbox_.Rows().empty());
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(DeliverDomainEventsTest, DeliveryWithoutReasonIsRefused) {
    const auto refused =
        Delivery::Compose(tenant_, student_, Channel::kEmail, "", "key", clock_.Now());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "delivery_reason_empty");
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: о переходе через порог узнают ОБЕ стороны.
/// Одно событие — две строки очереди, и вторая именно опекуну.
TEST_F(DeliverDomainEventsTest, CrossingAThresholdTellsBothSides) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::CapabilitiesWidened{
        Envelope(), student_, guardian_, "own_payments", 16});

    ASSERT_EQ(outbox_.Rows().size(), 2U) << "о переходе узнала только одна сторона";
    EXPECT_TRUE(outbox_.Rows().front().delivery.Recipient() == student_);
    EXPECT_TRUE(outbox_.Rows().back().delivery.Recipient() == guardian_);
    EXPECT_EQ(outbox_.Rows().back().delivery.Reason(), "identity.capabilities_widened");
}

TEST_F(DeliverDomainEventsTest, AnAdultWithoutAGuardianIsToldAlone) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::CapabilitiesWidened{
        Envelope(), student_, std::nullopt, "majority", 18});

    ASSERT_EQ(outbox_.Rows().size(), 1U);
    EXPECT_TRUE(outbox_.Rows().front().delivery.Recipient() == student_);
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: о самостоятельном поступке подопечного опекун
/// узнаёт ВСЕГДА, и повод у письма свой — не «что-то произошло», а что именно.
TEST_F(DeliverDomainEventsTest, EveryIndependentActReachesTheGuardian) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    using pdr::events::identity::WardAct;
    for (const auto act :
         {WardAct::kLessonRescheduled, WardAct::kLessonCancelled, WardAct::kReviewWritten}) {
        bus_.Publish(pdr::events::identity::WardActedAlone{Envelope(), guardian_, student_, act});
    }

    ASSERT_EQ(outbox_.Rows().size(), 3U);
    for (const auto& queued : outbox_.Rows()) {
        EXPECT_TRUE(queued.delivery.Recipient() == guardian_) << "письмо ушло не опекуну";
    }
    EXPECT_EQ(outbox_.Rows().front().delivery.Reason(), "identity.ward_rescheduled_lesson");
    EXPECT_EQ(outbox_.Rows().back().delivery.Reason(), "identity.ward_wrote_review")
        << "по поводу письма не отличить перенос занятия от отзыва";
}

/// Ученику о собственном поступке не пишут: он его только что и совершил.
TEST_F(DeliverDomainEventsTest, TheStudentIsNotToldAboutHimself) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::WardActedAlone{
        Envelope(), guardian_, student_, pdr::events::identity::WardAct::kReviewWritten});

    ASSERT_EQ(outbox_.Rows().size(), 1U);
    EXPECT_FALSE(outbox_.Rows().front().delivery.Recipient() == student_);
}

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: напоминание — ОТЛОЖЕННАЯ СТРОКА ОЧЕРЕДИ, а не
/// крон, перебирающий занятия. Проверяется это здесь единственным доступным
/// способом: строка появляется В ТОТ ЖЕ МОМЕНТ, что и запись на занятие, и срок
/// у неё в будущем.
TEST_F(DeliverDomainEventsTest, BookingLaysDownRemindersRightAway) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto starts_at = clock_.Now() + 48h;
    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), Numbered<core::LessonId>(100), tutor_, student_, starts_at});

    const auto day = outbox_.WithReason("notifications.reminder_day_before");
    const auto hour = outbox_.WithReason("notifications.reminder_hour_before");
    ASSERT_TRUE(day.has_value()) << "напоминания за сутки нет вовсе";
    ASSERT_TRUE(hour.has_value()) << "напоминания за час нет вовсе";

    EXPECT_TRUE(day->due_at == starts_at - 24h);
    EXPECT_TRUE(hour->due_at == starts_at - 1h);
    EXPECT_TRUE(day->due_at > clock_.Now()) << "срок напоминания не в будущем";
    EXPECT_EQ(outbox_.Counted("notifications.reminder_day_before"), 2U)
        << "напомнили не обеим сторонам";
}

/// Занятие через полчаса: напоминать «за сутки» и «за час» уже поздно, и строки
/// такой быть не должно. Иначе человек получает «занятие завтра» о занятии,
/// которое началось, — и это выглядит поломкой, а не заботой.
TEST_F(DeliverDomainEventsTest, ThePastIsNotWorthAReminder) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), Numbered<core::LessonId>(100), tutor_, student_, clock_.Now() + 30min});

    EXPECT_EQ(outbox_.Counted("notifications.reminder_day_before"), 0U);
    EXPECT_EQ(outbox_.Counted("notifications.reminder_hour_before"), 0U);
    EXPECT_EQ(outbox_.Counted("scheduling.lesson_booked"), 2U) << "о самой записи не сказали";
}

/// ПЕРЕНОС ДВИГАЕТ НАПОМИНАНИЕ, А НЕ ЗАВОДИТ ВТОРОЕ. Ключ намерения у него без
/// момента ровно затем: второй строкой человек получил бы напоминание о часе, в
/// который его уже никто не ждёт.
TEST_F(DeliverDomainEventsTest, RescheduleMovesTheReminderInsteadOfAddingOne) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto lesson = Numbered<core::LessonId>(100);
    const auto was = clock_.Now() + 48h;
    const auto becomes = clock_.Now() + 96h;

    bus_.Publish(pdr::events::scheduling::LessonBooked{Envelope(), lesson, tutor_, student_, was});
    bus_.Publish(pdr::events::scheduling::LessonRescheduled{Envelope(),
                                                            lesson,
                                                            tutor_,
                                                            student_,
                                                            student_,
                                                            was,
                                                            becomes,
                                                            core::Money::FromMinorUnits(0, rubles_),
                                                            RetentionReason::kFreeReschedule});

    EXPECT_EQ(outbox_.Counted("notifications.reminder_day_before"), 2U)
        << "напоминаний о переехавшем занятии стало больше, чем сторон";
    const auto day = outbox_.WithReason("notifications.reminder_day_before");
    ASSERT_TRUE(day.has_value());
    EXPECT_TRUE(day->due_at == becomes - 24h) << "напоминание осталось на прежнем часе";
}

/// ОТМЕНА ЗАБИРАЕТ НАПОМИНАНИЯ. Письмо «напоминаем: завтра в 17:00», пришедшее
/// после отмены, объясняет потом репетитор, а не мы.
TEST_F(DeliverDomainEventsTest, CancellationTakesTheRemindersBack) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto lesson = Numbered<core::LessonId>(100);
    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), lesson, tutor_, student_, clock_.Now() + 48h});
    ASSERT_EQ(outbox_.Counted("notifications.reminder_hour_before"), 2U);

    bus_.Publish(pdr::events::scheduling::LessonCancelled{Envelope(),
                                                          lesson,
                                                          tutor_,
                                                          student_,
                                                          student_,
                                                          CancelledBy::kStudent,
                                                          core::Money::FromMinorUnits(0, rubles_),
                                                          RetentionReason::kInsideFreeWindow});

    EXPECT_EQ(outbox_.Counted("notifications.reminder_day_before"), 0U);
    EXPECT_EQ(outbox_.Counted("notifications.reminder_hour_before"), 0U);
    EXPECT_EQ(outbox_.Counted("scheduling.lesson_cancelled"), 2U)
        << "об отмене не сказали обеим сторонам";
}

/// ОДИН И ТОТ ЖЕ ПОВТОР — ОДНА СТРОКА. Сценарий, отработавший дважды (повтор
/// запроса, второй проход подписки), не превращается в два письма: ключ
/// намерения детерминирован и в него не подмешано «сейчас».
TEST_F(DeliverDomainEventsTest, TheSameEventTwiceIsOneLetter) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const pdr::events::scheduling::LessonBooked booked{
        Envelope(), Numbered<core::LessonId>(100), tutor_, student_, clock_.Now() + 48h};
    bus_.Publish(booked);
    const auto after_first = outbox_.Rows().size();
    bus_.Publish(booked);

    EXPECT_EQ(outbox_.Rows().size(), after_first) << "тот же повод лёг в очередь второй строкой";
}

/// Два РАЗНЫХ события об одном человеке — два письма. Момент события в ключе
/// нужен именно для этого: отозвали опеку, вернули, отозвали снова — человек
/// обязан узнать оба раза.
TEST_F(DeliverDomainEventsTest, TwoRealEventsAreTwoLetters) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});
    clock_.Advance(1h);
    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});

    EXPECT_EQ(outbox_.Rows().size(), 2U) << "второе отзывание опеки потерялось молча";
}

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ СО СТОРОНЫ ОПОВЕЩЕНИЙ: письмо ОДНО НА ЧЕЛОВЕКА, а не
/// одно на занятие. Двенадцать писем «занятие отменено» за одну секунду — это не
/// забота, а поломка на вид.
TEST_F(DeliverDomainEventsTest, ABreakIsOneLetterPerPersonAndNotOnePerLesson) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto period = Numbered<core::TimeOffId>(7);

    /// Четыре отменённых занятия — и все четыре молчат: о них скажет одно
    /// событие о применённом решении.
    for (int number = 100; number < 104; ++number) {
        bus_.Publish(
            pdr::events::scheduling::LessonCancelled{Envelope(),
                                                     Numbered<core::LessonId>(number),
                                                     tutor_,
                                                     student_,
                                                     tutor_,
                                                     CancelledBy::kTutor,
                                                     core::Money::FromMinorUnits(0, rubles_),
                                                     RetentionReason::kTutorCancelled,
                                                     period});
    }
    ASSERT_EQ(outbox_.Counted("scheduling.lesson_cancelled"), 0U)
        << "об отмене по перерыву написали поштучно";

    bus_.Publish(pdr::events::scheduling::TimeOffApplied{Envelope(),
                                                         period,
                                                         tutor_,
                                                         clock_.Now() + 24h,
                                                         clock_.Now() + 14 * 24h,
                                                         TimeOffDecision::kCancel,
                                                         4,
                                                         {tutor_, student_}});

    EXPECT_EQ(outbox_.Counted("scheduling.time_off_applied"), 2U)
        << "писем не по одному на человека";
}

/// А ОБЫЧНАЯ ОТМЕНА ПИСЬМО ПО-ПРЕЖНЕМУ ЗАВОДИТ. Иначе «не писать о перерыве»
/// незаметно превратилось бы в «не писать об отменах вовсе».
TEST_F(DeliverDomainEventsTest, ACancellationOfItsOwnStillWritesToBothSides) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::scheduling::LessonCancelled{Envelope(),
                                                          Numbered<core::LessonId>(100),
                                                          tutor_,
                                                          student_,
                                                          student_,
                                                          CancelledBy::kStudent,
                                                          core::Money::FromMinorUnits(0, rubles_),
                                                          RetentionReason::kInsideFreeWindow});

    EXPECT_EQ(outbox_.Counted("scheduling.lesson_cancelled"), 2U);
}

/// НАПОМИНАНИЯ СНИМАЮТСЯ И ПРИ ОТМЕНЕ ПО ПЕРЕРЫВУ. Одно письмо на человека не
/// отменяет двенадцати ненужных «напоминаем: завтра в 17:00».
TEST_F(DeliverDomainEventsTest, ABreakTakesTheRemindersBackAllTheSame) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto lesson = Numbered<core::LessonId>(100);
    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), lesson, tutor_, student_, clock_.Now() + 48h});
    ASSERT_EQ(outbox_.Counted("notifications.reminder_hour_before"), 2U);

    bus_.Publish(pdr::events::scheduling::LessonCancelled{Envelope(),
                                                          lesson,
                                                          tutor_,
                                                          student_,
                                                          tutor_,
                                                          CancelledBy::kTutor,
                                                          core::Money::FromMinorUnits(0, rubles_),
                                                          RetentionReason::kTutorCancelled,
                                                          Numbered<core::TimeOffId>(7)});

    EXPECT_EQ(outbox_.Counted("notifications.reminder_day_before"), 0U);
    EXPECT_EQ(outbox_.Counted("notifications.reminder_hour_before"), 0U);
}

/// ВОЗВРАЩЕНИЕ: строка со сроком «за сутки до конца перерыва» ложится в очередь
/// сразу, при заведении периода. Задания, каждый день перебирающего перерывы,
/// не нужно вовсе — база умеет ждать лучше, чем цикл по таблице.
TEST_F(DeliverDomainEventsTest, TheReturnIsRemindedTheDayBeforeThePeriodEnds) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    const auto ends_at = clock_.Now() + 14 * 24h;
    bus_.Publish(pdr::events::scheduling::TimeOffDeclared{
        Envelope(), Numbered<core::TimeOffId>(7), tutor_, clock_.Now(), ends_at, std::nullopt});

    ASSERT_EQ(outbox_.Counted("notifications.reminder_before_return"), 1U);
    const auto queued = outbox_.WithReason("notifications.reminder_before_return");
    ASSERT_TRUE(queued.has_value());
    EXPECT_TRUE(queued->delivery.Recipient() == tutor_);
    EXPECT_TRUE(queued->due_at == ends_at - 24h);
}

/// Перерыв, заведённый задним числом и уже кончившийся, напоминания о выходе не
/// заводит вовсе: напоминать не о чем, и «выходите завтра» о позавчерашнем дне
/// выглядело бы поломкой.
TEST_F(DeliverDomainEventsTest, APeriodThatHasAlreadyEndedRemindsNobodyOfAnything) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::scheduling::TimeOffDeclared{Envelope(),
                                                          Numbered<core::TimeOffId>(7),
                                                          tutor_,
                                                          clock_.Now() - 14 * 24h,
                                                          clock_.Now() - 24h,
                                                          std::nullopt});

    EXPECT_EQ(outbox_.Counted("notifications.reminder_before_return"), 0U);
}

}  // namespace
}  // namespace pdr::notifications
