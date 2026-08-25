#include <set>
#include <string>

#include <gtest/gtest.h>
#include <pdr/testing/clock_contract.hpp>
#include <pdr/testing/id_generator_contract.hpp>

#include "core/types/ids.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_worlds.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "infrastructure/system_clock.hpp"

/// @file
/// Второй прогон contract-наборов часов и генератора идентификаторов — против
/// НАСТОЯЩИХ адаптеров. Базы им не нужно, поэтому оба набора идут на обеих
/// реализациях в каждом прогоне CI.

namespace pdr::testing {
namespace {

struct SystemClockWorld final {
    /// Настоящие часы идут сами: «дважды одно и то же» с них требовать нельзя.
    static constexpr bool kMovesOnItsOwn = true;

    const application::ports::Clock& Clock() const noexcept {
        return clock_;
    }

private:
    infrastructure::SystemClock clock_;
};

struct RandomIdGeneratorWorld final {
    const application::ports::IdGenerator& Generator() const noexcept {
        return generator_;
    }

private:
    infrastructure::RandomIdGenerator generator_;
};

}  // namespace

PDR_CLOCK_CONTRACT(Fake, FakeClockWorld);
PDR_CLOCK_CONTRACT(System, SystemClockWorld);

PDR_ID_GENERATOR_CONTRACT(Fake, FakeIdGeneratorWorld);
PDR_ID_GENERATOR_CONTRACT(Random, RandomIdGeneratorWorld);

namespace {

/// Свойства КОНКРЕТНОГО адаптера, а не контракта: фейк выдаёт подряд идущие
/// значения и версии UUID не имеет вовсе — требовать это от него нельзя.
TEST(RandomIdGenerator, IssuesUuidOfTheFourthVersion) {
    const infrastructure::RandomIdGenerator generator;

    for (int issued = 0; issued < 100; ++issued) {
        const auto id = generator.Next<core::PersonId>();
        EXPECT_EQ(id.AsBytes()[6] & 0xF0U, 0x40U) << "версия UUID не четвёртая";
        EXPECT_EQ(id.AsBytes()[8] & 0xC0U, 0x80U) << "вариант UUID не тот";
    }
}

/// А фейк — предсказуем, и на этом стоят ожидаемые значения в unit-тестах.
TEST(FakeIdGenerator, IssuesNumbersInOrder) {
    const FakeIdGenerator generator;

    EXPECT_EQ(generator.Next<core::PersonId>().ToString(), "00000000-0000-0000-0000-000000000001");
    EXPECT_EQ(generator.Next<core::PersonId>().ToString(), "00000000-0000-0000-0000-000000000002");
    EXPECT_EQ(generator.Issued(), 2U);
}

/// Настоящие часы показывают время после точки отсчёта фейка — иначе тест,
/// написанный на фейке, встретит в проде «прошлое».
TEST(SystemClock, RunsAfterTheFakeBeginning) {
    const infrastructure::SystemClock clock;

    EXPECT_FALSE(clock.Now() < FakeClock::DefaultStart());
}

}  // namespace
}  // namespace pdr::testing
