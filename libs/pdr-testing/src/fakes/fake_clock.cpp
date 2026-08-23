#include "fakes/fake_clock.hpp"

namespace pdr::testing {
namespace {

constexpr std::int64_t kDefaultStartSeconds = 1704067200;
constexpr std::int64_t kMicrosInSecond = 1000000;

}  // namespace

core::Instant FakeClock::DefaultStart() noexcept {
    return core::Instant::FromUnixMicros(kDefaultStartSeconds * kMicrosInSecond);
}

FakeClock::FakeClock() noexcept : FakeClock{DefaultStart()} {}

FakeClock::FakeClock(core::Instant start) noexcept : now_{start} {}

core::Instant FakeClock::Now() const {
    return now_;
}

void FakeClock::Advance(core::Instant::Duration delta) noexcept {
    now_ = now_ + delta;
}

void FakeClock::SetNow(core::Instant instant) noexcept {
    now_ = instant;
}

}  // namespace pdr::testing
