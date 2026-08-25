#pragma once

#include <cstdint>
#include <string>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::notifications {

enum class Channel : std::uint8_t {
    kEmail,
    kPush,
};

/// Строка исходящей очереди: кому, куда и по какому поводу.
///
/// Текста здесь нет намеренно. В очереди лежит повод — имя доменного события, —
/// а слова подставляет шаблон при отправке. Иначе текст, который видит человек,
/// оказывается зашит в бэкенде, и поменять его нельзя без выкатки.
class Delivery final {
public:
    static core::Result<Delivery> Compose(core::TenantId tenant,
                                          core::PersonId recipient,
                                          Channel channel,
                                          std::string reason,
                                          core::Instant created_at);

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }
    const core::PersonId& Recipient() const noexcept {
        return recipient_;
    }
    Channel DeliveryChannel() const noexcept {
        return channel_;
    }
    const std::string& Reason() const noexcept {
        return reason_;
    }
    core::Instant CreatedAt() const noexcept {
        return created_at_;
    }

private:
    Delivery(core::TenantId tenant,
             core::PersonId recipient,
             Channel channel,
             std::string reason,
             core::Instant created_at);

    core::TenantId tenant_;
    core::PersonId recipient_;
    Channel channel_;
    std::string reason_;
    core::Instant created_at_;
};

}  // namespace pdr::notifications
