#include "notifications/application/deliver_domain_events.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "events/identity/capabilities_widened.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "fakes/fake_clock.hpp"

namespace pdr::notifications {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

class FakeOutbox final : public ports::OutboxRepository {
public:
    void Enqueue(const Delivery& delivery) override {
        queued_.push_back(delivery);
    }

    const std::vector<Delivery>& Queued() const noexcept {
        return queued_;
    }

private:
    std::vector<Delivery> queued_;
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
};

TEST_F(DeliverDomainEventsTest, RevokedGuardianshipTurnsIntoALetter) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});

    ASSERT_EQ(outbox_.Queued().size(), 1U);
    const auto& queued = outbox_.Queued().front();
    EXPECT_TRUE(queued.Recipient() == guardian_);
    EXPECT_EQ(queued.DeliveryChannel(), Channel::kEmail);
    EXPECT_EQ(queued.Reason(), "identity.guardianship_revoked");
    EXPECT_TRUE(queued.CreatedAt() == clock_.Now());
    EXPECT_TRUE(queued.Tenant() == tenant_);
}

TEST_F(DeliverDomainEventsTest, BookedLessonNotifiesBothSides) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::scheduling::LessonBooked{
        Envelope(), Numbered<core::LessonId>(100), tutor_, student_, clock_.Now() + 48h});

    ASSERT_EQ(outbox_.Queued().size(), 2U);
    EXPECT_TRUE(outbox_.Queued()[0].Recipient() == student_);
    EXPECT_TRUE(outbox_.Queued()[1].Recipient() == tutor_);
    EXPECT_EQ(outbox_.Queued()[0].DeliveryChannel(), Channel::kPush);
}

TEST_F(DeliverDomainEventsTest, NobodySubscribedIsNotAFailure) {
    bus_.Publish(pdr::events::identity::GuardianshipRevoked{Envelope(), guardian_, student_});

    EXPECT_TRUE(outbox_.Queued().empty());
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(DeliverDomainEventsTest, DeliveryWithoutReasonIsRefused) {
    const auto refused = Delivery::Compose(tenant_, student_, Channel::kEmail, "", clock_.Now());

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

    ASSERT_EQ(outbox_.Queued().size(), 2U) << "о переходе узнала только одна сторона";
    EXPECT_TRUE(outbox_.Queued().front().Recipient() == student_);
    EXPECT_TRUE(outbox_.Queued().back().Recipient() == guardian_);
    EXPECT_EQ(outbox_.Queued().back().Reason(), "identity.capabilities_widened");
}

TEST_F(DeliverDomainEventsTest, AnAdultWithoutAGuardianIsToldAlone) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::CapabilitiesWidened{
        Envelope(), student_, std::nullopt, "majority", 18});

    ASSERT_EQ(outbox_.Queued().size(), 1U);
    EXPECT_TRUE(outbox_.Queued().front().Recipient() == student_);
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

    ASSERT_EQ(outbox_.Queued().size(), 3U);
    for (const auto& queued : outbox_.Queued()) {
        EXPECT_TRUE(queued.Recipient() == guardian_) << "письмо ушло не опекуну";
    }
    EXPECT_EQ(outbox_.Queued().front().Reason(), "identity.ward_rescheduled_lesson");
    EXPECT_EQ(outbox_.Queued().back().Reason(), "identity.ward_wrote_review")
        << "по поводу письма не отличить перенос занятия от отзыва";
}

/// Ученику о собственном поступке не пишут: он его только что и совершил.
TEST_F(DeliverDomainEventsTest, TheStudentIsNotToldAboutHimself) {
    DeliverDomainEvents deliver{outbox_};
    deliver.SubscribeTo(bus_);

    bus_.Publish(pdr::events::identity::WardActedAlone{
        Envelope(), guardian_, student_, pdr::events::identity::WardAct::kReviewWritten});

    ASSERT_EQ(outbox_.Queued().size(), 1U);
    EXPECT_FALSE(outbox_.Queued().front().Recipient() == student_);
}

}  // namespace
}  // namespace pdr::notifications
