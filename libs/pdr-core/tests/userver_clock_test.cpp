/// @file
/// Часы процесса: тот же contract-набор и одно свойство сверх него — их
/// ДВИГАЮТ ШТАТНО.
///
/// Свойство не косметическое: на нём стоит вся проверка сроков. Ключ повтора,
/// сессия и период задания протухают по времени, которое берёт фреймворк, и
/// проверить это можно либо подменой времени, либо сном. Сон в наборе запрещён
/// (docs/testing.md), значит подмена обязана работать — и это проверяется здесь,
/// а не подразумевается.
#include "infrastructure/userver_clock.hpp"

#include <chrono>

#include <pdr/testing/clock_contract.hpp>

#include <userver/utest/utest.hpp>
#include <userver/utils/datetime.hpp>
#include <userver/utils/mock_now.hpp>

namespace pdr::testing {
namespace {

struct UserverClockWorld final {
    /// Часы процесса идут сами, пока их не остановили подменой.
    static constexpr bool kMovesOnItsOwn = true;

    const application::ports::Clock& Clock() const noexcept {
        return clock_;
    }

private:
    infrastructure::UserverClock clock_;
};

}  // namespace

PDR_CLOCK_CONTRACT(Userver, UserverClockWorld);

TEST(UserverClock, TheContourMovesIt) {
    const infrastructure::UserverClock clock;

    userver::utils::datetime::MockNowSet(
        userver::utils::datetime::Stringtime("2027-03-01T12:00:00+0000"));
    const auto stopped = clock.Now();

    EXPECT_EQ(clock.Now(), stopped) << "подменённые часы идут сами: срок не проверить";

    userver::utils::datetime::MockSleep(std::chrono::hours{1});
    EXPECT_EQ(clock.Now().UnixMicros() - stopped.UnixMicros(), 3'600'000'000)
        << "часы не сдвинулись на штатную подмену — набор про сроки придётся писать на sleep";

    userver::utils::datetime::MockNowUnset();
}

}  // namespace pdr::testing
