#include "core/types/time.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/moment_builder.hpp"
#include "fakes/fake_clock.hpp"

namespace pdr::core {
namespace {

using namespace std::chrono_literals;

TEST(Instant, IsArithmeticInUtc) {
    const auto start = pdr::testing::MomentBuilder{}.Utc(2024, 1, 1).At(0, 0).Build();

    EXPECT_TRUE((start + 1h) - start == 3600s);
    EXPECT_TRUE((start + 90min) - 90min == start);
    EXPECT_TRUE(start < start + 1us);
    EXPECT_TRUE(start == Instant::FromUnixMicros(1704067200000000));
}

TEST(TimeZone, IsAName) {
    EXPECT_TRUE(TimeZone::Parse("Europe/Moscow").has_value());
    EXPECT_TRUE(TimeZone::Parse("UTC").has_value());
    EXPECT_TRUE(TimeZone::Parse("America/Argentina/Buenos_Aires").has_value());
    EXPECT_EQ(TimeZone::Parse("Europe/Moscow")->Name(), "Europe/Moscow");

    EXPECT_FALSE(TimeZone::Parse("").has_value());
    EXPECT_FALSE(TimeZone::Parse("/Moscow").has_value());
    EXPECT_FALSE(TimeZone::Parse("Europe/").has_value());
    EXPECT_FALSE(TimeZone::Parse("Europe//Moscow").has_value());
    EXPECT_FALSE(TimeZone::Parse("Europe/Mos cow").has_value());
    EXPECT_FALSE(TimeZone::Parse("1/Moscow").has_value());
    EXPECT_FALSE(TimeZone::Parse("A/B/C/D").has_value());

    // Момент и зона — разные типы: «17:00» без зоны собрать не из чего.
    EXPECT_TRUE(TimeZone::Parse("Europe/Moscow") != TimeZone::Parse("Asia/Novosibirsk"));
}

TEST(FakeClock, MovesTimeWithoutWaiting) {
    pdr::testing::FakeClock clock;
    const application::ports::Clock& port = clock;

    const auto start = port.Now();
    EXPECT_TRUE(start == pdr::testing::FakeClock::DefaultStart());
    EXPECT_TRUE(port.Now() == start) << "«сейчас» убежало между двумя вопросами";

    // Двое суток проходят мгновенно: тест на «отмену не позже чем за сутки»
    // не спит ни микросекунды.
    clock.Advance(48h);
    EXPECT_TRUE(port.Now() - start == 48h);

    const auto cancel_deadline = start + 24h;
    EXPECT_TRUE(port.Now() > cancel_deadline);

    clock.SetNow(start);
    EXPECT_TRUE(port.Now() == start);
    EXPECT_TRUE(port.Now() < cancel_deadline);
}

}  // namespace
}  // namespace pdr::core
