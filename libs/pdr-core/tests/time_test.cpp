#include "core/types/time.hpp"

#include <chrono>

#include "testing/check.hpp"
#include "testing/fake_clock.hpp"

namespace {

using pdr::core::Instant;
using pdr::core::TimeZone;
using namespace std::chrono_literals;

void InstantIsArithmeticInUtc() {
    const auto start = Instant::FromUnixMicros(1704067200000000);

    PDR_CHECK((start + 1h) - start == 3600s);
    PDR_CHECK((start + 90min) - 90min == start);
    PDR_CHECK(start < start + 1us);
    PDR_CHECK(start == Instant::FromUnixMicros(1704067200000000));
}

void TimeZoneIsAName() {
    PDR_CHECK(TimeZone::Parse("Europe/Moscow").has_value());
    PDR_CHECK(TimeZone::Parse("UTC").has_value());
    PDR_CHECK(TimeZone::Parse("America/Argentina/Buenos_Aires").has_value());
    PDR_CHECK(TimeZone::Parse("Europe/Moscow")->Name() == "Europe/Moscow");

    PDR_CHECK(!TimeZone::Parse("").has_value());
    PDR_CHECK(!TimeZone::Parse("/Moscow").has_value());
    PDR_CHECK(!TimeZone::Parse("Europe/").has_value());
    PDR_CHECK(!TimeZone::Parse("Europe//Moscow").has_value());
    PDR_CHECK(!TimeZone::Parse("Europe/Mos cow").has_value());
    PDR_CHECK(!TimeZone::Parse("1/Moscow").has_value());
    PDR_CHECK(!TimeZone::Parse("A/B/C/D").has_value());

    // Момент и зона — разные типы: «17:00» без зоны собрать не из чего.
    PDR_CHECK(TimeZone::Parse("Europe/Moscow") != TimeZone::Parse("Asia/Novosibirsk"));
}

void SubstitutedClockMovesTimeWithoutWaiting() {
    pdr::testing::FakeClock clock;
    const pdr::application::ports::Clock& port = clock;

    const auto start = port.Now();
    PDR_CHECK(start == pdr::testing::FakeClock::DefaultStart());
    PDR_CHECK(port.Now() == start);  // «сейчас» не убегает между вызовами

    // Двое суток проходят мгновенно: тест на «отмену не позже чем за сутки»
    // не спит ни микросекунды.
    clock.Advance(48h);
    PDR_CHECK(port.Now() - start == 48h);

    const auto cancel_deadline = start + 24h;
    PDR_CHECK(port.Now() > cancel_deadline);

    clock.SetNow(start);
    PDR_CHECK(port.Now() == start);
    PDR_CHECK(port.Now() < cancel_deadline);
}

}  // namespace

int main() {
    InstantIsArithmeticInUtc();
    TimeZoneIsAName();
    SubstitutedClockMovesTimeWithoutWaiting();
    return pdr::testing::Summary("core.time");
}
