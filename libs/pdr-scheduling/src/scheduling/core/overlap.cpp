#include "scheduling/core/overlap.hpp"

namespace pdr::scheduling {

bool Overlaps(const Lesson& first, const Lesson& second, core::Instant::Duration buffer) noexcept {
    return first.StartsAt() < second.EndsAt() + buffer &&
           second.StartsAt() < first.EndsAt() + buffer;
}

}  // namespace pdr::scheduling
