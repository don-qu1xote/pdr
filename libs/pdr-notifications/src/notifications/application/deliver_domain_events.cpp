#include "notifications/application/deliver_domain_events.hpp"

#include <string>

#include "events/identity/capabilities_widened.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/scheduling/lesson_booked.hpp"

namespace pdr::notifications {
namespace {

using CapabilitiesWidened = pdr::events::identity::CapabilitiesWidened;
using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;
using WardActedAlone = pdr::events::identity::WardActedAlone;

}  // namespace

DeliverDomainEvents::DeliverDomainEvents(ports::OutboxRepository& outbox) noexcept
    : outbox_{outbox} {}

void DeliverDomainEvents::Enqueue(const core::TenantId& tenant,
                                  const core::PersonId& recipient,
                                  Channel channel,
                                  std::string_view reason,
                                  core::Instant at) {
    const auto delivery = Delivery::Compose(tenant, recipient, channel, std::string{reason}, at);
    if (delivery.HasValue()) {
        outbox_.Enqueue(delivery.Value());
    }
}

void DeliverDomainEvents::SubscribeTo(events::Bus& bus) {
    bus.Subscribe<GuardianshipRevoked>([this](const GuardianshipRevoked& event) {
        Enqueue(event.envelope.tenant,
                event.guardian,
                Channel::kEmail,
                GuardianshipRevoked::kType,
                event.envelope.occurred_at);
    });

    bus.Subscribe<CapabilitiesWidened>([this](const CapabilitiesWidened& event) {
        Enqueue(event.envelope.tenant,
                event.student,
                Channel::kPush,
                CapabilitiesWidened::kType,
                event.envelope.occurred_at);
        if (event.guardian.has_value()) {
            Enqueue(event.envelope.tenant,
                    *event.guardian,
                    Channel::kEmail,
                    CapabilitiesWidened::kType,
                    event.envelope.occurred_at);
        }
    });

    bus.Subscribe<WardActedAlone>([this](const WardActedAlone& event) {
        Enqueue(event.envelope.tenant,
                event.guardian,
                Channel::kEmail,
                Name(event.act),
                event.envelope.occurred_at);
    });

    bus.Subscribe<LessonBooked>([this](const LessonBooked& event) {
        Enqueue(event.envelope.tenant,
                event.student,
                Channel::kPush,
                LessonBooked::kType,
                event.envelope.occurred_at);
        Enqueue(event.envelope.tenant,
                event.tutor,
                Channel::kPush,
                LessonBooked::kType,
                event.envelope.occurred_at);
    });
}

}  // namespace pdr::notifications
