#include "notifications/application/deliver_domain_events.hpp"

#include <chrono>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "events/identity/guardianship_revoked.hpp"
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

}  // namespace
}  // namespace pdr::notifications
