#include "notifications/application/dispatch_outbox.hpp"

#include <utility>

namespace pdr::notifications {

DispatchOutbox::DispatchOutbox(ports::OutboxQueue& queue,
                               const ports::NotificationSender& sender,
                               const ports::RetryPolicies& policies,
                               const application::ports::Clock& clock) noexcept
    : queue_{queue}, sender_{sender}, policies_{policies}, clock_{clock} {}

core::Result<DispatchReport> DispatchOutbox::Execute(int limit) const {
    const auto policy = policies_.Current();
    if (!policy.HasValue()) {
        return policy.Failure();
    }

    if (limit < 1) {
        return core::Error{core::ErrorKind::kValidation,
                           "outbox_batch_not_positive",
                           "проход, который берёт ноль строк, не разбирает очередь"};
    }

    const auto now = clock_.Now();
    auto claimed = queue_.Claim(now, policy.Value().Deadline(now), limit);

    DispatchReport report{};
    report.claimed = claimed.size();

    for (const auto& entry : claimed) {
        const auto sent = sender_.Send(entry.delivery);
        if (sent.HasValue()) {
            ++report.sent;
            queue_.Settle(ports::Settlement{
                entry.delivery.Tenant(), entry.id, OutboxState::kSent, now, std::string{}});
            continue;
        }

        /// Строку захватили — значит попытка уже посчитана, и спрашиваем мы про
        /// неё: «после стольких неудач пробовать ли ещё». Пустой ответ означает
        /// предел, а не ошибку.
        const auto again = policy.Value().Again(entry.attempts, now);
        if (again.has_value()) {
            ++report.retried;
            queue_.Settle(ports::Settlement{
                entry.delivery.Tenant(), entry.id, OutboxState::kPending, *again, std::string{}});
            continue;
        }

        /// СДАЛИСЬ. Причина — код последнего отказа: он же лежит в журнале
        /// рядом, и по нему видно, чинить ли адрес, узел или наш код. Строка
        /// остаётся в таблице: удалить её значит потерять единственный след
        /// того, что человеку не написали.
        ++report.gave_up;
        queue_.Settle(ports::Settlement{
            entry.delivery.Tenant(), entry.id, OutboxState::kGaveUp, now, sent.Failure().Code()});
    }

    return report;
}

}  // namespace pdr::notifications
