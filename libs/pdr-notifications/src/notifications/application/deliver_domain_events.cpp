#include "notifications/application/deliver_domain_events.hpp"

#include <string>

#include "events/identity/guardianship_revoked.hpp"
#include "events/scheduling/lesson_booked.hpp"

namespace pdr::notifications {
namespace {

using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;

}  // namespace

DeliverDomainEvents::DeliverDomainEvents(ports::OutboxRepository& outbox) noexcept
    : outbox_{outbox} {}

void DeliverDomainEvents::SubscribeTo(events::Bus& bus) {
    bus.Subscribe<GuardianshipRevoked>([this](const GuardianshipRevoked& event) {
        // Опекуну — почтой: отзыв опеки не то, о чём узнают из push-уведомления
        // между делом.
        const auto delivery = Delivery::Compose(event.envelope.tenant,
                                                event.guardian,
                                                Channel::kEmail,
                                                std::string{GuardianshipRevoked::kType},
                                                event.envelope.occurred_at);
        if (delivery.HasValue()) {
            outbox_.Enqueue(delivery.Value());
        }
    });

    bus.Subscribe<LessonBooked>([this](const LessonBooked& event) {
        const auto for_student = Delivery::Compose(event.envelope.tenant,
                                                   event.student,
                                                   Channel::kPush,
                                                   std::string{LessonBooked::kType},
                                                   event.envelope.occurred_at);
        if (for_student.HasValue()) {
            outbox_.Enqueue(for_student.Value());
        }

        const auto for_tutor = Delivery::Compose(event.envelope.tenant,
                                                 event.tutor,
                                                 Channel::kPush,
                                                 std::string{LessonBooked::kType},
                                                 event.envelope.occurred_at);
        if (for_tutor.HasValue()) {
            outbox_.Enqueue(for_tutor.Value());
        }
    });
}

}  // namespace pdr::notifications
