/// @file
/// Отображение доменных типов на типы Postgres и вставка пачкой — на НАСТОЯЩЕЙ
/// базе (PDR-DB-04).
///
/// Проверяется то, чего не видно ни компилятору, ни фейку:
///
/// * идентификатор доезжает до колонки `uuid` БЕЗ `ToString()` и читается
///   обратно тем же значением. Отображение работает, а не «собирается»;
/// * момент доезжает до `timestamptz` без ручного перевода;
/// * роли вставляются ОДНИМ запросом: `RowsAffected()` у него равен числу
///   ролей, а не единице;
/// * выборка, потерявшая колонку, ломает разбор строки в структуру. Разбор по
///   строковому имени колонки этого не заметил бы: он молча прочитал бы
///   остальные и оставил недостающее пустым.
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/exceptions.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/person.hpp"
#include "identity/infrastructure/auth/postgres_participant_directory.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/random_id_generator.hpp"

namespace pdr::identity {
namespace {

constexpr std::array<std::string_view, 5> kSetUp{
    R"(create table if not exists identity_tenant (
           tenant_id  uuid        not null primary key,
           name       text        not null,
           tz         text        not null,
           created_at timestamptz not null default now()
       ))",
    R"(create table if not exists identity_person (
           tenant_id    uuid not null,
           id           uuid not null,
           display_name text not null,
           email        text,
           tz           text not null,
           created_at   timestamptz not null default now(),
           born_on      date,
           constraint identity_person_pk primary key (tenant_id, id),
           constraint identity_person_email_unique unique (tenant_id, email)
       ))",
    R"(create table if not exists identity_role_assignment (
           tenant_id  uuid        not null,
           id         uuid        not null,
           person_id  uuid        not null,
           role       text        not null,
           granted_at timestamptz not null default now(),
           revoked_at timestamptz,
           constraint identity_role_assignment_pk primary key (tenant_id, id)
       ))",
    "delete from identity_role_assignment",
    "delete from identity_person",
};

core::TenantId SomeTenant() {
    core::IdBytes bytes{};
    bytes[0] = 11;
    return core::TenantId::FromBytes(bytes);
}

core::PersonId SomePerson() {
    core::IdBytes bytes{};
    bytes[0] = 22;
    bytes[15] = 3;
    return core::PersonId::FromBytes(bytes);
}

class DirectoryTypesTest : public ::testing::Test {
protected:
    DirectoryTypesTest() {
        for (const auto statement : kSetUp) {
            local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                         std::string{statement});
        }
    }

    userver::storages::postgres::Transaction Begin() {
        return local_.GetCluster()->Begin(userver::storages::postgres::ClusterHostType::kMaster,
                                          userver::storages::postgres::TransactionOptions{});
    }

    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::RandomIdGenerator ids_;
};

}  // namespace

UTEST_F(DirectoryTypesTest, IdentifierTravelsAsUuid) {
    const auto tenant = SomeTenant();
    const auto person = SomePerson();
    const auto born = core::Instant::FromUnixMicros(1'700'000'000'000'000);

    auto transaction = Begin();
    transaction.Execute(
        "insert into identity_person (tenant_id, id, display_name, tz, created_at) "
        "values ($1, $2, 'Нина', 'Europe/Moscow', $3)",
        tenant,
        person,
        born);

    const auto found = transaction.Execute(
        "select id::text, created_at from identity_person where tenant_id = $1 and id = $2",
        tenant,
        person);
    transaction.Commit();

    ASSERT_EQ(found.Size(), 1U) << "строка не нашлась: идентификатор доехал не тем значением";
    const auto stored = found.Front();
    EXPECT_EQ(stored["id"].As<std::string>(), person.ToString());
    EXPECT_EQ(
        infrastructure::db::AsInstant(stored["created_at"].As<infrastructure::db::Timestamptz>()),
        born)
        << "момент доехал не тем значением";
}

UTEST_F(DirectoryTypesTest, EveryRoleGoesInOneStatement) {
    const auto tenant = SomeTenant();
    const auto person = SomePerson();

    const std::vector<core::TenantId> tenants{tenant, tenant, tenant};
    const std::vector<core::PersonId> owners{person, person, person};
    std::vector<core::StrongId<struct RowTag>> rows;
    for (int index = 0; index < 3; ++index) {
        rows.push_back(ids_.Next<core::StrongId<struct RowTag>>());
    }
    const std::vector<std::string> roles{"owner", "tutor", "student"};

    auto transaction = Begin();
    const auto inserted =
        transaction.Execute(sql::kIdentityRoleAssignmentGrant, tenants, rows, owners, roles);
    transaction.Commit();

    EXPECT_EQ(inserted.RowsAffected(), 3U)
        << "три роли вставились не одним запросом: пачка развалилась на строки";
}

UTEST_F(DirectoryTypesTest, EnrolmentGrantsEveryRoleThroughTheAdapter) {
    const auto tenant = SomeTenant();
    const auto born = BirthDate::Of(2001, 4, 12);
    ASSERT_TRUE(born.HasValue()) << born.Failure().Detail();

    infrastructure::db::TenantContext tenants{local_.GetCluster()};
    auto scope = tenants.Open(tenant,
                              userver::storages::postgres::ClusterHostType::kMaster,
                              userver::storages::postgres::TransactionOptions{});
    PostgresParticipantDirectory directory{scope, ids_};

    const auto enrolled =
        directory.Enrol(tenant,
                        ports::Enrolment{Person{SomePerson(), std::nullopt, born.Value()},
                                         RoleSet::Of({Role::kOwner, Role::kTutor, Role::kStudent}),
                                         "Нина",
                                         *core::TimeZone::Parse("Europe/Moscow")});
    ASSERT_TRUE(enrolled.HasValue()) << enrolled.Failure().Detail();
    scope.Commit();

    const auto stored = local_.GetCluster()->Execute(
        userver::storages::postgres::ClusterHostType::kMaster,
        "select role from identity_role_assignment where person_id = $1 order by role",
        SomePerson());

    ASSERT_EQ(stored.Size(), 3U) << "роли выданы не все: пачка развалилась";
    EXPECT_EQ(stored[0]["role"].As<std::string>(), "owner");
    EXPECT_EQ(stored[1]["role"].As<std::string>(), "student");
    EXPECT_EQ(stored[2]["role"].As<std::string>(), "tutor");
}

UTEST_F(DirectoryTypesTest, ALostColumnBreaksTheRowStruct) {
    auto transaction = Begin();
    const auto narrower = transaction.Execute(
        "select null::text as person_id, now() as created_at, now() as expires_at, "
        "null::timestamptz as revoked_at, 'a' as user_agent_hash");
    transaction.Commit();

    EXPECT_THROW(narrower.AsSingleRow<IdentitySessionFindRow>(userver::storages::postgres::kRowTag),
                 userver::storages::postgres::InvalidTupleSizeRequested)
        << "выборка без колонки разобралась в структуру: состав не сверяется";
}

}  // namespace pdr::identity
