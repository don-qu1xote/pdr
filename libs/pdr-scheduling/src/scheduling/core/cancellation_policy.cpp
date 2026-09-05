#include "scheduling/core/cancellation_policy.hpp"

namespace pdr::scheduling {

core::Result<CancellationPolicy> CancellationPolicy::Compose(Window free_window,
                                                             core::Percent late_retention,
                                                             core::Percent no_show_retention,
                                                             int free_reschedules) {
    if (free_window < Window::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "cancellation_window_negative",
                           "окно отмены не бывает отрицательным"};
    }
    if (free_reschedules < 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "cancellation_free_moves_negative",
                           "бесплатных переносов не бывает меньше нуля"};
    }

    return CancellationPolicy{free_window, late_retention, no_show_retention, free_reschedules};
}

bool CancellationPolicy::Free(core::Instant starts_at, core::Instant now) const noexcept {
    return starts_at - now >= std::chrono::duration_cast<core::Instant::Duration>(free_window_);
}

}  // namespace pdr::scheduling
