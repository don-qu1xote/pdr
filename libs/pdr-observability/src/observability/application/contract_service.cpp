#include "observability/application/contract_service.hpp"

#include <utility>

namespace pdr::observability {

ContractService::ContractService(ports::ProductEventStream& stream) noexcept : stream_{stream} {}

core::Result<void> ContractService::Record(const core::TenantId& tenant,
                                           std::string_view type,
                                           int version,
                                           Role actor,
                                           core::Instant occurred_at,
                                           Fields fields) {
    auto event = ProductEvent::Compose(
        tenant, std::string{type}, version, actor, occurred_at, std::move(fields));
    if (!event) {
        return event.Failure();
    }

    stream_.Record(event.Value());
    return {};
}

}  // namespace pdr::observability
