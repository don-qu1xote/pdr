#include "notifications/core/delivery.hpp"

#include <utility>

namespace pdr::notifications {

Delivery::Delivery(core::TenantId tenant,
                   core::PersonId recipient,
                   Channel channel,
                   std::string reason,
                   std::string dedup_key,
                   core::Instant created_at)
    : tenant_{std::move(tenant)},
      recipient_{std::move(recipient)},
      channel_{channel},
      reason_{std::move(reason)},
      dedup_key_{std::move(dedup_key)},
      created_at_{created_at} {}

core::Result<Delivery> Delivery::Compose(core::TenantId tenant,
                                         core::PersonId recipient,
                                         Channel channel,
                                         std::string reason,
                                         std::string dedup_key,
                                         core::Instant created_at) {
    if (reason.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "delivery_reason_empty",
                           "письмо без повода не отправляется"};
    }

    /// Пустой ключ — это «каждое письмо уникально», то есть отсутствие защиты от
    /// повтора при доставке, которая повторы обещает. Такую строку лучше не
    /// класть вовсе, чем положить и однажды отправить трижды.
    if (dedup_key.empty()) {
        return core::Error{core::ErrorKind::kValidation,
                           "delivery_dedup_key_empty",
                           "письмо без ключа намерения не кладётся в очередь"};
    }

    return Delivery{std::move(tenant),
                    std::move(recipient),
                    channel,
                    std::move(reason),
                    std::move(dedup_key),
                    created_at};
}

std::string_view Name(Channel channel) noexcept {
    switch (channel) {
        case Channel::kEmail:
            return "email";
        case Channel::kPush:
            return "push";
    }
    return "email";
}

}  // namespace pdr::notifications
