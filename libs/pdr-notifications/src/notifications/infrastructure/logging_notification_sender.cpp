#include "notifications/infrastructure/logging_notification_sender.hpp"

#include <string>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::notifications {

core::Result<void> LoggingNotificationSender::Send(const Delivery& delivery) const {
    LOG_INFO() << "оповещение отправлено"
               << userver::logging::LogExtra{
                      {{infrastructure::observe::kOutboxReasonField, delivery.Reason()},
                       {infrastructure::observe::kOutboxKeyField, delivery.DedupKey()},
                       {infrastructure::observe::kOutboxChannelField,
                        std::string{Name(delivery.DeliveryChannel())}}}};
    return {};
}

}  // namespace pdr::notifications
