/// @file
/// ПОСЛАБЛЕНИЯ ОКОН НА НАСТОЯЩЕЙ БАЗЕ.
///
/// На фейках проверено, что домен отклоняет ужесточение и что действующие окна
/// складываются из умолчаний и послабления (tests/booking_window_test.cpp,
/// tests/booking_windows_in_force_test.cpp). Здесь проверяется то, чего у фейка
/// нет вовсе:
///
///   * NULL В КОЛОНКЕ ДОЕЗЖАЕТ ДО ДОМЕНА ПУСТОТОЙ, а не нулём. Разница между
///     «можно вплоть до начала» и «мы про это не договаривались» живёт в схеме
///     ровно одним способом, и способ этот проверяется только базой;
///   * послабление у пары ОДНО: второе выданное заменяет первое, а не ложится
///     рядом. Это первичный ключ, а не наша аккуратность;
///   * пустая строка отвергается схемой: послабление, в котором ничего не
///     ослаблено, только сбивает с толку того, кто его потом читает.
#include "scheduling/infrastructure/postgres_booking_relief.hpp"

#include <chrono>
#include <optional>
#include <string>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "builders/identifiers.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling_ground.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant Noon() {
    return core::Instant::FromUnixMicros(1'800'000'000'000'000);
}

BookingWindows Windows(std::optional<BookingWindows::Notice> book,
                       std::optional<BookingWindows::Notice> horizon = std::nullopt,
                       std::optional<BookingWindows::Notice> reschedule = std::nullopt,
                       std::optional<BookingWindows::Notice> cancel = std::nullopt) {
    const auto composed = BookingWindows::Compose(book, horizon, reschedule, cancel);
    EXPECT_TRUE(composed.HasValue());
    return composed.Value();
}

class BookingReliefLiveTest : public ::testing::Test {
protected:
    BookingReliefLiveTest() {
        ApplySchedulingSchema(local_.GetCluster());
        OpenPractice(local_.GetCluster(), ContractGround::Tenant());
        Enrol(ContractGround::Tutor());
        Enrol(ContractGround::Student());
    }

    void Enrol(const core::PersonId& person) {
        local_.GetCluster()->Execute(
            userver::storages::postgres::ClusterHostType::kMaster,
            "INSERT INTO identity_person (tenant_id, id, display_name, tz) "
            "VALUES ($1, $2, 'человек', 'Europe/Moscow') ON CONFLICT DO NOTHING",
            ContractGround::Tenant(),
            person);
    }

    template<class Work>
    auto InScope(Work&& work) {
        auto scope = tenants_.Open(ContractGround::Tenant(),
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresBookingRelief relief{scope};
        auto said = work(relief);
        scope.Commit();
        return said;
    }

    core::Result<void> Grant(const BookingWindows& windows) {
        return InScope([&](PostgresBookingRelief& relief) {
            return relief.Grant(BookingRelief{ContractGround::Tenant(),
                                              ContractGround::Tutor(),
                                              ContractGround::Student(),
                                              windows,
                                              ContractGround::Tutor(),
                                              Noon()});
        });
    }

    std::optional<BookingWindows> Given() {
        return InScope([&](PostgresBookingRelief& relief) {
            return relief.For(
                ContractGround::Tenant(), ContractGround::Tutor(), ContractGround::Student());
        });
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
};

}  // namespace

/// Пары без строки — обычный случай, а не отказ: у большинства послаблений нет
/// и никогда не будет.
UTEST_F(BookingReliefLiveTest, APairWithoutARowHasNoRelaxation) {
    EXPECT_FALSE(Given().has_value());
}

/// ПУСТОТА ОСТАЁТСЯ ПУСТОТОЙ, А НОЛЬ — НУЛЁМ. Разница между ними — это разница
/// между «отменить можно всегда» и «отменить можно вплоть до самого начала», и
/// теряется она молча.
UTEST_F(BookingReliefLiveTest, ANullColumnComesBackAsNoLimitAndZeroComesBackAsZero) {
    ASSERT_TRUE(Grant(Windows(BookingWindows::Notice::zero(), std::nullopt, 30min)).HasValue());

    const auto given = Given();

    ASSERT_TRUE(given.has_value());
    EXPECT_EQ(given->MinNoticeBook(), BookingWindows::Notice::zero());
    EXPECT_EQ(given->MaxHorizonBook(), std::nullopt);
    EXPECT_EQ(given->MinNoticeReschedule(), 30min);
    EXPECT_EQ(given->MinNoticeCancel(), std::nullopt);
}

/// Минуты доезжают минутами: «за сорок минут» — срок, которого репетитор вправе
/// захотеть, и перевод в часах потерял бы его.
UTEST_F(BookingReliefLiveTest, MinutesSurviveTheRoundTrip) {
    ASSERT_TRUE(Grant(Windows(40min, 45 * 24h)).HasValue());

    const auto given = Given();

    ASSERT_TRUE(given.has_value());
    EXPECT_EQ(given->MinNoticeBook(), 40min);
    EXPECT_EQ(given->MaxHorizonBook(), 45 * 24h);
}

/// ПОСЛАБЛЕНИЕ У ПАРЫ ОДНО. Держит это первичный ключ, а не порядок вызовов:
/// две строки на пару означали бы вопрос «какая из них действует», на который
/// отвечать некому.
UTEST_F(BookingReliefLiveTest, ASecondGrantReplacesTheFirst) {
    ASSERT_TRUE(Grant(Windows(12h)).HasValue());
    ASSERT_TRUE(Grant(Windows(1h)).HasValue());

    EXPECT_EQ(Given()->MinNoticeBook(), 1h);
    EXPECT_EQ(local_.GetCluster()
                  ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                            "SELECT count(*)::bigint FROM scheduling_booking_window")
                  .AsSingleRow<std::int64_t>(),
              1);
}

/// Послабление, в котором ничего не ослаблено, отвергается схемой: строка,
/// которая ничего не значит, хуже её отсутствия — её читают и ищут в ней смысл.
UTEST_F(BookingReliefLiveTest, ARelaxationThatRelaxesNothingIsRefusedBySchema) {
    EXPECT_THROW(static_cast<void>(Grant(BookingWindows::Anything())),
                 userver::storages::postgres::CheckViolation);
}

}  // namespace pdr::scheduling::testing
