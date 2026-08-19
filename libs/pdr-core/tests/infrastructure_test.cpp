#include <set>
#include <string>

#include "core/types/ids.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "infrastructure/system_clock.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"

namespace {

void SystemClockAnswersSomethingSane() {
    const pdr::infrastructure::SystemClock clock;
    const pdr::application::ports::Clock& port = clock;

    const auto first = port.Now();
    const auto second = port.Now();

    // Настоящие часы идут вперёд и показывают время после точки отсчёта фейка.
    PDR_CHECK(first >= pdr::testing::FakeClock::DefaultStart());
    PDR_CHECK(second >= first);
}

void GeneratorGivesDistinctUuidsOfTheFourthVersion() {
    const pdr::infrastructure::RandomIdGenerator generator;

    std::set<std::string> seen;
    for (int i = 0; i < 100; ++i) {
        const auto id = generator.Next<pdr::core::PersonId>();
        seen.insert(id.ToString());

        PDR_CHECK((id.AsBytes()[6] & 0xF0U) == 0x40U);
        PDR_CHECK((id.AsBytes()[8] & 0xC0U) == 0x80U);
    }

    PDR_CHECK(seen.size() == 100);
}

}  // namespace

int main() {
    SystemClockAnswersSomethingSane();
    GeneratorGivesDistinctUuidsOfTheFourthVersion();
    return pdr::testing::Summary("core.infrastructure");
}
