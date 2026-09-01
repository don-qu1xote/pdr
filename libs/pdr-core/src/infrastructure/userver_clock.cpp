#include "infrastructure/userver_clock.hpp"

#include <chrono>

#include <userver/utils/datetime.hpp>

namespace pdr::infrastructure {

core::Instant UserverClock::Now() const {
    const auto now = userver::utils::datetime::Now().time_since_epoch();
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now);
    return core::Instant::FromUnixMicros(micros.count());
}

}  // namespace pdr::infrastructure
