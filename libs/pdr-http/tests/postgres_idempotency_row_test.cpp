/// @file
/// Состав выборки занятия ключа — на НАСТОЯЩЕЙ базе (PDR-DB-04).
///
/// Строку занятия разбирает структура, написанная руками: у запроса стоит
/// `@no-dto`, и порождать её не из чего (причина — в самом файле запроса).
/// Значит, состав выборки и состав структуры держатся не сборкой, а этим
/// тестом: лишняя колонка в запросе штатным разбором НЕ ловится — userver про
/// неё только предупреждает в журнале, — а переставленные колонки одного типа
/// не ловятся вовсе.
///
/// Поэтому здесь закреплены имена колонок и их порядок. Правка запроса без
/// правки структуры красит прогон и называет расхождение.
#include <array>
#include <string>
#include <string_view>

#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::infrastructure::http {
namespace {

constexpr std::array<std::string_view, 2> kSetUp{
    R"(create table if not exists http_idempotency_key (
           tenant_id           uuid        not null,
           key                 text        not null,
           request_fingerprint text        not null,
           state               text        not null,
           response_status     integer,
           response_body       text,
           created_at          timestamptz not null default now(),
           expires_at          timestamptz not null,
           constraint http_idempotency_key_pk primary key (tenant_id, key)
       ))",
    "delete from http_idempotency_key",
};

/// Порядок и имена — те же, что у полей ClaimRow в
/// src/infrastructure/http/postgres_idempotency_keys.cpp.
constexpr std::array<std::string_view, 5> kClaimColumns{
    "request_fingerprint",
    "state",
    "response_status",
    "response_body",
    "mine",
};

core::TenantId SomeTenant() {
    core::IdBytes bytes{};
    bytes[0] = 42;
    return core::TenantId::FromBytes(bytes);
}

class ClaimRowTest : public ::testing::Test {
protected:
    ClaimRowTest() {
        for (const auto statement : kSetUp) {
            local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                         std::string{statement});
        }
    }

    userver::storages::postgres::utest::ClusterLocal local_;
};

}  // namespace

UTEST_F(ClaimRowTest, ClaimKeepsItsColumnsAndTheirOrder) {
    auto transaction =
        local_.GetCluster()->Begin(userver::storages::postgres::ClusterHostType::kMaster,
                                   userver::storages::postgres::TransactionOptions{});
    const auto claimed = transaction.Execute(sql::kHttpIdempotencyKeyClaim,
                                             SomeTenant(),
                                             std::string{"some-key"},
                                             std::string{"fingerprint"},
                                             core::Instant::FromUnixMicros(1'900'000'000'000'000));
    transaction.Commit();

    ASSERT_EQ(claimed.FieldCount(), kClaimColumns.size())
        << "состав выборки занятия ключа изменился, а ClaimRow — нет";
    for (std::size_t index = 0; index < kClaimColumns.size(); ++index) {
        EXPECT_EQ(claimed.Front()[index].Name(), kClaimColumns[index])
            << "колонка " << index << " переехала: разбор структурой прочтёт не то";
    }
}

}  // namespace pdr::infrastructure::http
