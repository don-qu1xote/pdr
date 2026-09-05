#include "scheduling/application/set_availability.hpp"

namespace pdr::scheduling {

SetAvailability::SetAvailability(ports::AvailabilityRepository& availability) noexcept
    : availability_{availability} {}

core::Result<void> SetAvailability::Execute(const Request& request) const {
    return availability_.Replace(request.tenant, request.tutor, request.availability);
}

}  // namespace pdr::scheduling
