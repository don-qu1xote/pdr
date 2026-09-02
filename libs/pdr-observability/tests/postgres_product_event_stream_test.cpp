/// @file
/// Продуктовый поток на НАСТОЯЩЕЙ базе.
///
/// На фейке проверено, что запись собирается и что обезличивание отказывает
/// (tests/product_event_test.cpp). Здесь проверяется вторая половина, которой у
/// фейка нет: строка доходит до таблицы, поля лежат в jsonb ШТАТНЫМ разбором, а
/// не сборкой строки, и ограничение обезличивания в базе отвергает то же, что
/// отвергает домен.
#include "observability/infrastructure/postgres_product_event_stream.hpp"

#include <array>
#include <cstdint>
#include <string>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/random_id_generator.hpp"

namespace pdr::observability {
namespace {

/// Запросы поодиночке: `Cluster::Execute` готовит запрос, а подготовленный
/// состоит ровно из одной команды.
constexpr std::array<std::string_view, 3> kSetUp{
    R"(create table if not exists identity_tenant (
           tenant_id  uuid        not null primary key,
           name       text        not null,
           tz         text        not null,
           created_at timestamptz not null default now()
       ))",
    R"(create table if not exists observability_product_event (
           tenant_id   uuid        not null,
           id          uuid        not null,
           type        text        not null,
           version     integer     not null,
           actor_role  text        not null,
           occurred_at timestamptz not null,
           recorded_at timestamptz not null default now(),
           fields      jsonb       not null,
           constraint observability_product_event_pk primary key (tenant_id, id),
           constraint observability_product_event_fields_are_anonymous
               check (not jsonb_path_exists(
                   fields,
                   '$.keyvalue() ? (@.key like_regex "_id|^id|person|email|phone|login|name")'
               ))
       ))",
    "delete from observability_product_event",
};

core::TenantId SomeTenant() {
    core::IdBytes bytes{};
    bytes[0] = 7;
    return core::TenantId::FromBytes(bytes);
}

class ProductStreamTest : public ::testing::Test {
protected:
    ProductStreamTest() {
        for (const auto statement : kSetUp) {
            local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                         std::string{statement});
        }
    }

    userver::formats::json::Value StoredFields() {
        return userver::formats::json::FromString(
            local_.GetCluster()
                ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                          "SELECT fields::text FROM observability_product_event")
                .AsSingleRow<std::string>());
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
    infrastructure::RandomIdGenerator ids_;
};

}  // namespace

UTEST_F(ProductStreamTest, FieldsKeepTheirKind) {
    const auto tenant = SomeTenant();
    const auto event = ProductEvent::Compose(tenant,
                                             "scheduling.lesson_completed",
                                             1,
                                             Role::kTutor,
                                             core::Instant::FromUnixMicros(1'700'000'000'000'000),
                                             Fields{{"minutes_late", Value::Minutes(5)},
                                                    {"was_first", Value::Flag(true)},
                                                    {"reason", Value::Code("moved")}});
    ASSERT_TRUE(event.HasValue()) << event.Failure().Detail();

    auto scope = tenants_.Open(tenant,
                               userver::storages::postgres::ClusterHostType::kMaster,
                               userver::storages::postgres::TransactionOptions{});
    PostgresProductEventStream stream{scope, ids_};
    stream.Record(event.Value());
    scope.Commit();

    const auto stored = StoredFields();
    EXPECT_EQ(stored["minutes_late"].As<std::int64_t>(), 5)
        << "число уехало строкой: выгрузка сравнивала бы «5» с 5";
    EXPECT_TRUE(stored["was_first"].As<bool>());
    EXPECT_EQ(stored["reason"].As<std::string>(), "moved");
}

/// База отвергает то же, что домен: правило одно, мест применения два.
UTEST_F(ProductStreamTest, DatabaseRefusesAName) {
    const auto tenant = SomeTenant();
    auto scope = tenants_.Open(tenant,
                               userver::storages::postgres::ClusterHostType::kMaster,
                               userver::storages::postgres::TransactionOptions{});
    PostgresProductEventStream stream{scope, ids_};

    EXPECT_THROW(
        scope.Session().Execute("INSERT INTO observability_product_event "
                                "(tenant_id, id, type, version, actor_role, occurred_at, fields) "
                                "VALUES ($1::uuid, gen_random_uuid(), 'x.y', 1, 'tutor', now(), "
                                "'{\"person_id\": \"kate\"}'::jsonb)",
                                tenant.ToString()),
        std::exception)
        << "поле, именующее человека, вставилось: главное ограничение таблицы не работает";
}

}  // namespace pdr::observability
