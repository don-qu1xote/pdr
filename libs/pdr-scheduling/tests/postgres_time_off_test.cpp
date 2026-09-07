/// @file
/// ПЕРЕРЫВЫ НА НАСТОЯЩЕЙ БАЗЕ.
///
/// На фейках проверено то, что решает домен: период заводится задним числом,
/// причина необязательна, одно решение применяется ко всем занятиям
/// (tests/time_off_test.cpp, tests/declare_time_off_test.cpp,
/// tests/resolve_time_off_test.cpp). Здесь — то, чего у фейка нет вовсе:
///
///   * МЕСТНЫЕ ДНИ В МОМЕНТЫ СЧИТАЕТ БАЗА, и считает по настоящей базе IANA.
///     Границу дня двигает перевод часов, и проверить это можно только против
///     Postgres: у ядра таблицы переводов нет намеренно;
///   * ДВА ПЕРЕРЫВА У ОДНОГО ЧЕЛОВЕКА НЕ НАКЛАДЫВАЮТСЯ — это ограничение
///     `exclude using gist`, а не наша аккуратность;
///   * РЕШЕНИЕ ПРИНИМАЮТ ОДИН РАЗ: второе применение отсекает сам запрос, а не
///     чтение перед ним, — между чтением и записью помещается чужое обращение;
///   * NULL в причине доезжает до домена пустотой, а не словом.
#include "scheduling/infrastructure/postgres_time_off.hpp"

#include <chrono>
#include <cstdint>
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
#include "scheduling/infrastructure/postgres_local_days.hpp"
#include "scheduling_ground.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant Noon() {
    return core::Instant::FromUnixMicros(1'800'000'000'000'000);
}

core::Date On(int year, unsigned month, unsigned day) {
    return core::Date::Compose(year, month, day).Value();
}

core::TimeZone Berlin() {
    return core::TimeZone::Parse("Europe/Berlin").value();
}

class TimeOffLiveTest : public ::testing::Test {
protected:
    TimeOffLiveTest() {
        ApplySchedulingSchema(local_.GetCluster());
        OpenPractice(local_.GetCluster(), ContractGround::Tenant());
        Enrol(ContractGround::Tutor());
        Enrol(ContractGround::Student());
        local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                     "DELETE FROM scheduling_time_off");
    }

    void Enrol(const core::PersonId& person) {
        local_.GetCluster()->Execute(
            userver::storages::postgres::ClusterHostType::kMaster,
            "INSERT INTO identity_person (tenant_id, id, display_name, tz) "
            "VALUES ($1, $2, 'человек', 'Europe/Moscow') ON CONFLICT DO NOTHING",
            ContractGround::Tenant(),
            person);
    }

    /// КОММИТ ТОЛЬКО ПРИ УСПЕХЕ — так же, как это делает ручка.
    ///
    /// Отказ ограничения (наложение периодов, незнакомая зона) роняет всю
    /// транзакцию Postgres: адаптер превращает его в значение, но продолжать в
    /// этой транзакции уже нечего, и коммитить её нельзя. Набор, который
    /// коммитит всегда, проверял бы не отказ, а падение на коммите.
    template<class Work>
    auto InScope(Work&& work) {
        auto scope = tenants_.Open(ContractGround::Tenant(),
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresTimeOff periods{scope};
        PostgresLocalDays days{scope};
        auto said = work(periods, days);
        if (said.HasValue()) {
            scope.Commit();
        }
        return said;
    }

    /// Чтение: коммитить нечего, но закрыть область надо.
    template<class Work>
    auto Reading(Work&& work) {
        auto scope = tenants_.Open(ContractGround::Tenant(),
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresTimeOff periods{scope};
        auto said = work(periods);
        scope.Commit();
        return said;
    }

    TimeOff Period(int number,
                   core::Date from,
                   core::Date to,
                   std::optional<TimeOffReason> reason = std::nullopt,
                   core::PersonId person = ContractGround::Tutor()) {
        const auto composed = TimeOff::Compose(Numbered<core::TimeOffId>(number),
                                               ContractGround::Tenant(),
                                               person,
                                               from,
                                               to,
                                               reason,
                                               Berlin(),
                                               ContractGround::Tutor());
        EXPECT_TRUE(composed.HasValue());
        return composed.Value();
    }

    core::Result<void> Declare(const TimeOff& period) {
        return InScope([&](PostgresTimeOff& periods, PostgresLocalDays&) {
            return periods.Save(period, Noon());
        });
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
};

}  // namespace

/// Период уезжает в базу и возвращается тем же — вместе с зоной, в которой его
/// назвали, и без причины, которой не назвали.
UTEST_F(TimeOffLiveTest, APeriodSurvivesTheRoundTripWithoutAReason) {
    const auto period = Period(1, On(2026, 8, 3), On(2026, 8, 16));
    ASSERT_TRUE(Declare(period).HasValue());

    const auto found = Reading([&](PostgresTimeOff& periods) {
        return periods.Find(ContractGround::Tenant(), Numbered<core::TimeOffId>(1));
    });

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->From(), On(2026, 8, 3));
    EXPECT_EQ(found->To(), On(2026, 8, 16));
    EXPECT_FALSE(found->Reason().has_value()) << "пустая причина вернулась словом";
    EXPECT_TRUE(found->Zone() == Berlin());
    EXPECT_TRUE(found->Person() == ContractGround::Tutor());
}

UTEST_F(TimeOffLiveTest, ANamedReasonComesBackNamed) {
    ASSERT_TRUE(
        Declare(Period(1, On(2026, 8, 3), On(2026, 8, 16), TimeOffReason::kSickness)).HasValue());

    const auto found = Reading([&](PostgresTimeOff& periods) {
        return periods.Find(ContractGround::Tenant(), Numbered<core::TimeOffId>(1));
    });

    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->Reason(), TimeOffReason::kSickness);
}

/// ДВА ПЕРЕРЫВА НА ОДНИ ДНИ — ОТКАЗ БАЗЫ, а не наша проверка перед вставкой.
/// Между чтением «а нет ли уже» и записью помещается чужое обращение.
UTEST_F(TimeOffLiveTest, TwoOverlappingPeriodsOfOnePersonAreRefusedByTheSchema) {
    ASSERT_TRUE(Declare(Period(1, On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    const auto refused = Declare(Period(2, On(2026, 8, 10), On(2026, 8, 20)));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "time_off_overlaps");
}

/// Соседние периоды, которые лишь касаются концами, — разные периоды: отпуск по
/// шестнадцатое и болезнь с семнадцатого не накладываются.
UTEST_F(TimeOffLiveTest, PeriodsThatMerelyTouchAreTwoPeriods) {
    ASSERT_TRUE(Declare(Period(1, On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    EXPECT_TRUE(Declare(Period(2, On(2026, 8, 17), On(2026, 8, 20))).HasValue());
}

UTEST_F(TimeOffLiveTest, AnotherPersonMayBeAwayOnTheSameDays) {
    ASSERT_TRUE(Declare(Period(1, On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    EXPECT_TRUE(
        Declare(Period(2, On(2026, 8, 3), On(2026, 8, 16), std::nullopt, ContractGround::Student()))
            .HasValue());
}

/// РЕШЕНИЕ ПРИНИМАЮТ ОДИН РАЗ, и отсекает второе сам запрос.
UTEST_F(TimeOffLiveTest, ASecondDecisionIsRefusedByTheStatementItself) {
    ASSERT_TRUE(Declare(Period(1, On(2026, 8, 3), On(2026, 8, 16))).HasValue());

    const auto first = InScope([&](PostgresTimeOff& periods, PostgresLocalDays&) {
        return periods.Decide(ContractGround::Tenant(),
                              Numbered<core::TimeOffId>(1),
                              TimeOffDecision::kCancel,
                              SeriesDecision::kSkip,
                              Noon());
    });
    ASSERT_TRUE(first.HasValue());

    const auto again = InScope([&](PostgresTimeOff& periods, PostgresLocalDays&) {
        return periods.Decide(ContractGround::Tenant(),
                              Numbered<core::TimeOffId>(1),
                              TimeOffDecision::kPostpone,
                              SeriesDecision::kShift,
                              Noon());
    });

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "time_off_already_decided");
    EXPECT_EQ(local_.GetCluster()
                  ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                            "SELECT lessons_decided FROM scheduling_time_off "
                            " WHERE id = $1",
                            Numbered<core::TimeOffId>(1))
                  .AsSingleRow<std::string>(),
              "cancel");
}

UTEST_F(TimeOffLiveTest, ADecisionOnAPeriodThatDoesNotExistIsRefused) {
    const auto missing = InScope([&](PostgresTimeOff& periods, PostgresLocalDays&) {
        return periods.Decide(ContractGround::Tenant(),
                              Numbered<core::TimeOffId>(999),
                              TimeOffDecision::kCancel,
                              SeriesDecision::kSkip,
                              Noon());
    });

    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Failure().Code(), "time_off_already_decided");
}

/// ГЛАВНОЕ, ЧЕГО У ФЕЙКА НЕТ: границу дня двигает перевод часов, и знает об этом
/// только настоящая база IANA.
///
/// Отпуск с 25 октября по 1 ноября 2026-го начинается по берлинскому летнему
/// времени (UTC+2) и кончается по зимнему (UTC+1): часы переводят в ночь на
/// 25-е. Полночь начала — 24 октября 22:00 UTC, полночь после конца — 1 ноября
/// 23:00 UTC. Отрезок длиннее восьми суток ровно на этот час.
UTEST_F(TimeOffLiveTest, TheClockChangeMovesTheEdgeOfTheDayAndThePeriodKnowsIt) {
    const auto span = InScope([&](PostgresTimeOff&, PostgresLocalDays& days) {
        return days.Between(On(2026, 10, 25), On(2026, 11, 1), Berlin());
    });

    ASSERT_TRUE(span.HasValue());
    EXPECT_EQ(span.Value().Length(), 8 * 24h + 1h)
        << "перевод часов не сдвинул границу дня: отрезок посчитан без базы зон";
}

/// Имя зоны проверяет база — у ядра списка зон нет вовсе, `TimeZone::Parse`
/// смотрит на форму. Отказ приходит значением: это ответ человеку, а не авария.
UTEST_F(TimeOffLiveTest, AZoneThatDoesNotExistIsRefusedInWords) {
    const auto refused = InScope([&](PostgresTimeOff&, PostgresLocalDays& days) {
        return days.Between(
            On(2026, 8, 3), On(2026, 8, 16), core::TimeZone::Parse("Europe/Mordor").value());
    });

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "time_zone_unknown");
}

/// Конец отрезка ИСКЛЮЧИТЕЛЬНЫЙ: «по шестнадцатое включительно» это «до
/// семнадцатого». Зона без переводов делает счёт очевидным.
UTEST_F(TimeOffLiveTest, TheLastDayOfThePeriodIsInsideItWhole) {
    const auto span = InScope([&](PostgresTimeOff&, PostgresLocalDays& days) {
        return days.Between(
            On(2026, 8, 3), On(2026, 8, 16), core::TimeZone::Parse("Europe/Moscow").value());
    });

    ASSERT_TRUE(span.HasValue());
    EXPECT_EQ(span.Value().Length(), 14 * 24h);
}

}  // namespace pdr::scheduling::testing
