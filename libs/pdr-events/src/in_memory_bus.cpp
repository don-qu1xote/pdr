#include "events/in_memory_bus.hpp"

#include <utility>

namespace pdr::events {

void InMemoryBus::PublishErased(std::type_index type, const void* event) {
    ++published_;

    const auto found = handlers_.find(type);
    if (found == handlers_.end()) {
        return;
    }

    for (const auto& handler : found->second) {
        handler(event);
    }
}

void InMemoryBus::SubscribeErased(std::type_index type, ErasedHandler handler) {
    handlers_[type].push_back(std::move(handler));
}

}  // namespace pdr::events
