#include "scheduling/application/get_availability.hpp"

#include <utility>

namespace pdr::scheduling {

GetAvailability::GetAvailability(const ports::AvailabilityRepository& availability) noexcept
    : availability_{availability} {}

core::Result<Availability> GetAvailability::Execute(const Request& request) const {
    auto found = availability_.Of(request.tenant, request.tutor);
    if (!found.has_value()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "availability_not_set",
                           "репетитор ещё не говорил, когда он работает"};
    }
    return std::move(*found);
}

}  // namespace pdr::scheduling
