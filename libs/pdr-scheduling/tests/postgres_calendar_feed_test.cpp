/// @file
/// ПОДПИСКА НА КАЛЕНДАРЬ И ПРАВИЛА ЗОН НА НАСТОЯЩЕЙ БАЗЕ.
///
/// На фейках проверено то, что решает домен: перевыпуск убивает старую ссылку,
/// серия уезжает правилом, отменённое не показывается
/// (tests/calendar_feed_test.cpp, tests/icalendar_test.cpp). Здесь — то, чего у
/// фейка нет вовсе:
///
///   * ПЕРЕВОДЫ ЧАСОВ ИЗ НАСТОЯЩЕЙ БАЗЫ IANA. Даты перевода не выдумываются и
///     не помнятся наизусть — их называет Postgres, у которого эта таблица
///     есть и обновляется вместе с ним;
///   * подписка у человека ОДНА: это первичный ключ, а не наша аккуратность,
///     и перевыпуск заменяет строку, а не кладёт вторую;
///   * отпечаток уникален в пределах кабинета — иначе один секрет открывал бы
///     два расписания;
///   * незнакомая зона отвергается базой, а не нами: списка зон у ядра нет.
#include <chrono>
#include <cstdint>
#include <string>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "infrastructure/db/postgres_time_zone_rules.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/sha256_digests.hpp"
#include "scheduling/infrastructure/postgres_calendar_feeds.hpp"
#include "scheduling_ground.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

using namespace std::chrono_literals;
using pdr::testing::MomentBuilder;
using pdr::testing::Numbered;

core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, 0).Build();
}

core::TimeZone Named(const char* zone) {
    return core::TimeZone::Parse(zone).value();
}

class CalendarFeedLiveTest : public ::testing::Test {
protected:
    CalendarFeedLiveTest() {
        ApplySchedulingSchema(local_.GetCluster());
        OpenPractice(local_.GetCluster(), ContractGround::Tenant());
        Enrol(ContractGround::Tutor());
        Enrol(ContractGround::Student());
        local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                     "DELETE FROM scheduling_calendar_feed");
    }

    void Enrol(const core::PersonId& person) {
        local_.GetCluster()->Execute(
            userver::storages::postgres::ClusterHostType::kMaster,
            "INSERT INTO identity_person (tenant_id, id, display_name, tz) "
            "VALUES ($1, $2, 'человек', 'Europe/Moscow') ON CONFLICT DO NOTHING",
            ContractGround::Tenant(),
            person);
    }

    /// Коммит только при успехе — так же, как это делает ручка: отказ
    /// ограничения роняет всю транзакцию, и коммитить её нельзя.
    template<class Work>
    auto InScope(Work&& work) {
        auto scope = tenants_.Open(ContractGround::Tenant(),
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresCalendarFeeds feeds{scope};
        infrastructure::db::PostgresTimeZoneRules zones{scope};
        auto said = work(feeds, zones);
        if (said.HasValue()) {
            scope.Commit();
        }
        return said;
    }

    template<class Work>
    auto Reading(Work&& work) {
        auto scope = tenants_.Open(ContractGround::Tenant(),
                                   userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
        PostgresCalendarFeeds feeds{scope};
        auto said = work(feeds);
        scope.Commit();
        return said;
    }

    core::Result<void> Issue(const std::string& secret,
                             const core::PersonId& person = ContractGround::Tutor()) {
        const auto feed = CalendarFeed::Compose(
            ContractGround::Tenant(), person, digests_.Of(secret), CalendarNaming::kWithoutNames);
        EXPECT_TRUE(feed.HasValue());
        return InScope(
            [&](PostgresCalendarFeeds& feeds, infrastructure::db::PostgresTimeZoneRules&) {
                return feeds.Issue(feed.Value(), Utc(2026, 7, 1, 12));
            });
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
    infrastructure::Sha256Digests digests_;
};

}  // namespace

UTEST_F(CalendarFeedLiveTest, ALinkOpensExactlyOneSchedule) {
    ASSERT_TRUE(Issue("первый-секрет").HasValue());

    const auto found = Reading([&](PostgresCalendarFeeds& feeds) {
        return feeds.ByDigest(ContractGround::Tenant(), digests_.Of("первый-секрет"));
    });

    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->Person() == ContractGround::Tutor());
    EXPECT_EQ(found->Naming(), CalendarNaming::kWithoutNames);
}

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ НА ЖИВОЙ БАЗЕ: перевыпуск делает старую ссылку
/// нерабочей, и делает это ЗАМЕНОЙ СТРОКИ, а не второй строкой рядом.
UTEST_F(CalendarFeedLiveTest, ReissuingLeavesExactlyOneRowAndKillsTheOldLink) {
    ASSERT_TRUE(Issue("первый-секрет").HasValue());
    ASSERT_TRUE(Issue("второй-секрет").HasValue());

    const auto old_one = Reading([&](PostgresCalendarFeeds& feeds) {
        return feeds.ByDigest(ContractGround::Tenant(), digests_.Of("первый-секрет"));
    });
    const auto fresh = Reading([&](PostgresCalendarFeeds& feeds) {
        return feeds.ByDigest(ContractGround::Tenant(), digests_.Of("второй-секрет"));
    });

    EXPECT_FALSE(old_one.has_value()) << "старая ссылка всё ещё открывает расписание";
    EXPECT_TRUE(fresh.has_value());
    EXPECT_EQ(local_.GetCluster()
                  ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                            "SELECT count(*)::bigint FROM scheduling_calendar_feed")
                  .AsSingleRow<std::int64_t>(),
              1);
}

/// САМОГО СЕКРЕТА В БАЗЕ НЕТ — только отпечаток. Утёкшая копия базы не даёт ни
/// одной работающей ссылки, и проверяется это на самой строке.
UTEST_F(CalendarFeedLiveTest, TheSecretItselfIsNowhereInTheRow) {
    ASSERT_TRUE(Issue("первый-секрет").HasValue());

    const auto stored = local_.GetCluster()
                            ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                      "SELECT secret_digest FROM scheduling_calendar_feed")
                            .AsSingleRow<std::string>();

    EXPECT_EQ(stored, digests_.Of("первый-секрет").Value());
    EXPECT_EQ(stored.find("первый-секрет"), std::string::npos);
}

UTEST_F(CalendarFeedLiveTest, TheSettingSurvivesAReissue) {
    ASSERT_TRUE(Issue("первый-секрет").HasValue());
    ASSERT_TRUE(
        InScope([&](PostgresCalendarFeeds& feeds, infrastructure::db::PostgresTimeZoneRules&) {
            return feeds.Rename(
                ContractGround::Tenant(), ContractGround::Tutor(), CalendarNaming::kWithNames);
        }).HasValue());

    ASSERT_TRUE(Issue("второй-секрет").HasValue());

    const auto naming = Reading([&](PostgresCalendarFeeds& feeds) {
        return feeds.NamingOf(ContractGround::Tenant(), ContractGround::Tutor());
    });
    EXPECT_EQ(naming, CalendarNaming::kWithNames) << "починка утечки заодно поменяла настройку";
}

UTEST_F(CalendarFeedLiveTest, WhatWasNeverIssuedOpensNothing) {
    const auto missing = Reading([&](PostgresCalendarFeeds& feeds) {
        return feeds.ByDigest(ContractGround::Tenant(), digests_.Of("не выдавали"));
    });

    EXPECT_FALSE(missing.has_value());
}

/// ГЛАВНОЕ, ЧЕГО У ФЕЙКА НЕТ: переводы часов из настоящей базы IANA. Даты
/// перевода здесь не выдуманы и не вспомнены — их называет Postgres.
UTEST_F(CalendarFeedLiveTest, TheClockChangesComeFromTheRealZoneDatabase) {
    const auto rules = InScope([&](PostgresCalendarFeeds&,
                                   infrastructure::db::PostgresTimeZoneRules& zones) {
        return zones.For(Named("Europe/Berlin"),
                         core::TimeRange::Compose(Utc(2026, 1, 1, 0), Utc(2027, 1, 1, 0)).Value());
    });

    ASSERT_TRUE(rules.HasValue());
    EXPECT_EQ(rules.Value().Initial(), 1h);
    ASSERT_EQ(rules.Value().Shifts().size(), 2U) << "за год у Берлина два перевода часов";
    EXPECT_EQ(rules.Value().Shifts()[0].at, Utc(2026, 3, 29, 1));
    EXPECT_EQ(rules.Value().Shifts()[0].offset, 2h);
    EXPECT_EQ(rules.Value().Shifts()[1].at, Utc(2026, 10, 25, 1));
    EXPECT_EQ(rules.Value().Shifts()[1].offset, 1h);
}

/// А У МОСКВЫ ПЕРЕВОДОВ НЕТ ВОВСЕ, и это тоже ответ, а не пустота: смещение
/// названо, переводов ноль.
UTEST_F(CalendarFeedLiveTest, AZoneWithoutClockChangesStillNamesItsOffset) {
    const auto rules = InScope([&](PostgresCalendarFeeds&,
                                   infrastructure::db::PostgresTimeZoneRules& zones) {
        return zones.For(Named("Europe/Moscow"),
                         core::TimeRange::Compose(Utc(2026, 1, 1, 0), Utc(2027, 1, 1, 0)).Value());
    });

    ASSERT_TRUE(rules.HasValue());
    EXPECT_EQ(rules.Value().Initial(), 3h);
    EXPECT_TRUE(rules.Value().Shifts().empty());
}

/// Имя зоны проверяет база — у ядра списка зон нет вовсе, `TimeZone::Parse`
/// смотрит на форму. Отказ приходит значением: это ответ человеку, а не авария.
UTEST_F(CalendarFeedLiveTest, AZoneThatDoesNotExistIsRefusedInWords) {
    const auto refused = InScope([&](PostgresCalendarFeeds&,
                                     infrastructure::db::PostgresTimeZoneRules& zones) {
        return zones.For(Named("Europe/Mordor"),
                         core::TimeRange::Compose(Utc(2026, 1, 1, 0), Utc(2026, 2, 1, 0)).Value());
    });

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "time_zone_unknown");
}

}  // namespace pdr::scheduling::testing
