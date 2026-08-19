#include "infrastructure/system_clock.hpp"

#include <chrono>

namespace pdr::infrastructure {

core::Instant SystemClock::Now() const {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now);
    return core::Instant::FromUnixMicros(micros.count());
}

}  // namespace pdr::infrastructure
