#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::notifications {

enum class Channel : std::uint8_t {
    kEmail,
    kPush,
};

/// ПИСЬМО, КОТОРОЕ ПРЕДСТОИТ ОТПРАВИТЬ: кому, куда и по какому поводу.
///
/// Текста здесь нет намеренно. В очереди лежит повод — имя доменного события, —
/// а слова подставляет шаблон при отправке. Иначе текст, который видит человек,
/// оказывается зашит в бэкенде, и поменять его нельзя без выкатки.
///
/// КЛЮЧ НАМЕРЕНИЯ — ПОЛЕ, БЕЗ КОТОРОГО ОЧЕРЕДЬ НЕ РАБОТАЕТ.
///
/// Он отвечает на «это то же самое письмо или другое». Доставка идёт «не менее
/// одного раза»: сеть моргнула в неудачный момент — и одно событие уедет дважды.
/// По ключу повтор узнаёт и получатель, и мы сами при вставке. Ключ обязан быть
/// детерминированным: «напомнить о занятии 7f3c… за час» — один и тот же ключ у
/// любого процесса и в любом прогоне. Ключ, в который подмешано «сейчас», — не
/// ключ вовсе.
///
/// СРОКА ОТПРАВКИ ЗДЕСЬ НЕТ, и это не пропуск. «Когда отправлять» — свойство
/// очереди, а не письма: одно и то же письмо кладут немедленным и отложенным, и
/// поле в письме означало бы, что письмо знает про очередь. Срок приходит
/// вторым доводом в `OutboxRepository::Enqueue`.
class Delivery final {
public:
    static core::Result<Delivery> Compose(core::TenantId tenant,
                                          core::PersonId recipient,
                                          Channel channel,
                                          std::string reason,
                                          std::string dedup_key,
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
    const std::string& DedupKey() const noexcept {
        return dedup_key_;
    }
    core::Instant CreatedAt() const noexcept {
        return created_at_;
    }

private:
    Delivery(core::TenantId tenant,
             core::PersonId recipient,
             Channel channel,
             std::string reason,
             std::string dedup_key,
             core::Instant created_at);

    core::TenantId tenant_;
    core::PersonId recipient_;
    Channel channel_;
    std::string reason_;
    std::string dedup_key_;
    core::Instant created_at_;
};

std::string_view Name(Channel channel) noexcept;

}  // namespace pdr::notifications
