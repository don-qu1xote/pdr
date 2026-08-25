#include "notifications/core/delivery.hpp"

#include <utility>

namespace pdr::notifications {

Delivery::Delivery(core::TenantId tenant,
                   core::PersonId recipient,
                   Channel channel,
                   std::string reason,
                   core::Instant created_at)
    : tenant_{std::move(tenant)},
      recipient_{std::move(recipient)},
      channel_{channel},
      reason_{std::move(reason)},
      created_at_{created_at} {}

core::Result<Delivery> Delivery::Compose(core::TenantId tenant,
                                         core::PersonId recipient,
                                         Channel channel,
                                         std::string reason,
                                         core::Instant created_at) {
    if (reason.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "delivery_reason_empty",
                           "письмо без повода не отправляется"};
    }

    return Delivery{
        std::move(tenant), std::move(recipient), channel, std::move(reason), created_at};
}

}  // namespace pdr::notifications
