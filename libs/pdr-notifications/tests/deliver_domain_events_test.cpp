#include "notifications/application/deliver_domain_events.hpp"

#include <chrono>
#include <vector>

#include "events/identity/guardianship_revoked.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::notifications::Channel;
using pdr::notifications::DeliverDomainEvents;
using pdr::notifications::Delivery;

class FakeOutbox final : public pdr::notifications::ports::OutboxRepository {
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

struct Fixture final {
    pdr::testing::FakeIdGenerator ids;
    pdr::testing::FakeClock clock;
    pdr::events::InMemoryBus bus;
    FakeOutbox outbox;

    pdr::core::TenantId tenant{ids.Next<pdr::core::TenantId>()};
    pdr::core::PersonId guardian{ids.Next<pdr::core::PersonId>()};
    pdr::core::PersonId student{ids.Next<pdr::core::PersonId>()};
    pdr::core::PersonId tutor{ids.Next<pdr::core::PersonId>()};

    pdr::events::Envelope Envelope() const {
        return {tenant, clock.Now()};
    }
};

void RevokedGuardianshipTurnsIntoALetter() {
    Fixture fixture;
    DeliverDomainEvents deliver{fixture.outbox};
    deliver.SubscribeTo(fixture.bus);

    fixture.bus.Publish(pdr::events::identity::GuardianshipRevoked{
        fixture.Envelope(), fixture.guardian, fixture.student});

    PDR_CHECK(fixture.outbox.Queued().size() == 1);
    const auto& queued = fixture.outbox.Queued().front();
    PDR_CHECK(queued.Recipient() == fixture.guardian);
    PDR_CHECK(queued.DeliveryChannel() == Channel::kEmail);
    PDR_CHECK(queued.Reason() == "identity.guardianship_revoked");
    PDR_CHECK(queued.CreatedAt() == fixture.clock.Now());
    PDR_CHECK(queued.Tenant() == fixture.tenant);
}

void BookedLessonNotifiesBothSides() {
    Fixture fixture;
    DeliverDomainEvents deliver{fixture.outbox};
    deliver.SubscribeTo(fixture.bus);

    fixture.bus.Publish(
        pdr::events::scheduling::LessonBooked{fixture.Envelope(),
                                              fixture.ids.Next<pdr::core::LessonId>(),
                                              fixture.tutor,
                                              fixture.student,
                                              fixture.clock.Now() + 48h});

    PDR_CHECK(fixture.outbox.Queued().size() == 2);
    PDR_CHECK(fixture.outbox.Queued()[0].Recipient() == fixture.student);
    PDR_CHECK(fixture.outbox.Queued()[1].Recipient() == fixture.tutor);
    PDR_CHECK(fixture.outbox.Queued()[0].DeliveryChannel() == Channel::kPush);
}

void NobodySubscribedIsNotAFailure() {
    Fixture fixture;

    // Подписки нет — событие просто проходит мимо, издатель об этом не узнает.
    fixture.bus.Publish(pdr::events::identity::GuardianshipRevoked{
        fixture.Envelope(), fixture.guardian, fixture.student});

    PDR_CHECK(fixture.outbox.Queued().empty());
    PDR_CHECK(fixture.bus.Published() == 1);
}

void DeliveryWithoutReasonIsRefused() {
    Fixture fixture;

    const auto refused = Delivery::Compose(
        fixture.tenant, fixture.student, Channel::kEmail, "", fixture.clock.Now());

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Code() == "delivery_reason_empty");
}

}  // namespace

int main() {
    RevokedGuardianshipTurnsIntoALetter();
    BookedLessonNotifiesBothSides();
    NobodySubscribedIsNotAFailure();
    DeliveryWithoutReasonIsRefused();
    return pdr::testing::Summary("notifications.deliver");
}
