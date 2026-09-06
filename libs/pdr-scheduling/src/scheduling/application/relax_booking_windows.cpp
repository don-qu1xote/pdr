#include "scheduling/application/relax_booking_windows.hpp"

namespace pdr::scheduling {

RelaxBookingWindows::RelaxBookingWindows(const ports::BookingWindowDefaults& defaults,
                                         ports::BookingWindowRelief& relief,
                                         const application::ports::Clock& clock) noexcept
    : defaults_{defaults}, relief_{relief}, clock_{clock} {}

core::Result<BookingWindows> RelaxBookingWindows::Execute(const Request& request) const {
    const auto base = defaults_.ForPractice(request.tenant);
    if (!base.HasValue()) {
        return base.Failure();
    }

    const auto relaxed = Relax(base.Value(), request.windows);
    if (!relaxed.HasValue()) {
        return relaxed.Failure();
    }

    const auto written = relief_.Grant(BookingRelief{request.tenant,
                                                     request.tutor,
                                                     request.student,
                                                     relaxed.Value(),
                                                     request.granted_by,
                                                     clock_.Now()});
    if (!written.HasValue()) {
        return written.Failure();
    }

    return relaxed.Value();
}

}  // namespace pdr::scheduling
