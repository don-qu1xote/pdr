#include "scheduling/application/windows_in_force.hpp"

namespace pdr::scheduling {

WindowsInForce::WindowsInForce(const ports::BookingWindowDefaults& defaults,
                               const ports::BookingWindowRelief& relief) noexcept
    : defaults_{defaults}, relief_{relief} {}

core::Result<BookingWindows> WindowsInForce::For(const core::TenantId& tenant,
                                                 const core::PersonId& tutor,
                                                 const core::PersonId& student) const {
    const auto base = defaults_.ForPractice(tenant);
    if (!base.HasValue()) {
        return base.Failure();
    }

    return Relax(base.Value(), relief_.For(tenant, tutor, student));
}

}  // namespace pdr::scheduling
