#include "notifications/core/outbox_entry.hpp"

namespace pdr::notifications {

std::string_view Name(OutboxState state) noexcept {
    switch (state) {
        case OutboxState::kPending:
            return "pending";
        case OutboxState::kSent:
            return "sent";
        case OutboxState::kGaveUp:
            return "gave_up";
        case OutboxState::kBoundary:
            break;
    }
    return "pending";
}

}  // namespace pdr::notifications
