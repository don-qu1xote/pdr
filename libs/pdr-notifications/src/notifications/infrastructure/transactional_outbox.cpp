#include "notifications/infrastructure/transactional_outbox.hpp"

#include "events/in_memory_bus.hpp"
#include "notifications/application/deliver_domain_events.hpp"
#include "notifications/infrastructure/postgres_outbox.hpp"

namespace pdr::notifications {
namespace {

/// Шина обращения и всё, что на ней сидит, — одним объектом.
///
/// Порядок объявления полей здесь работающий, а не косметический: шина
/// объявлена ПОСЛЕДНЕЙ и потому разрушается ПЕРВОЙ. Обработчики на ней держат
/// указатель на сценарий; разрушься сценарий раньше шины, и разрушение шины
/// прошлось бы по мёртвым замыканиям.
class OutboxAttachment final : public events::Attachment {
public:
    OutboxAttachment(infrastructure::db::ScopedTenantContext& session,
                     const application::ports::IdGenerator& ids)
        : outbox_{session, ids}, delivering_{outbox_} {
        delivering_.SubscribeTo(bus_);
    }

    events::Bus& Events() override {
        return bus_;
    }

private:
    PostgresOutbox outbox_;
    DeliverDomainEvents delivering_;
    events::InMemoryBus bus_;
};

}  // namespace

TransactionalOutbox::TransactionalOutbox(const userver::components::ComponentConfig& config,
                                         const userver::components::ComponentContext& context)
    : ComponentBase{config, context} {}

std::unique_ptr<events::Attachment> TransactionalOutbox::Attach(
    infrastructure::db::ScopedTenantContext& session) const {
    return std::make_unique<OutboxAttachment>(session, ids_);
}

}  // namespace pdr::notifications
