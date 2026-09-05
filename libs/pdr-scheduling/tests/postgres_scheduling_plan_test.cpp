/// @file
/// ПЛАН ЗАПРОСА — машиной, а не глазами на ревью.
///
/// Индексы заведены под два настоящих запроса порта (`ports::LessonRepository`):
/// занятия репетитора за диапазон и занятия участника за диапазон. Заведены —
/// не значит используются: достаточно поменять порядок колонок в индексе или
/// обернуть колонку функцией в запросе, и Postgres молча пойдёт по всей
/// таблице. Проверка спрашивает у самого Postgres, каким путём он пойдёт.
///
/// Строк здесь тысячи намеренно: на пустой таблице Postgres выбирает Seq Scan и
/// оказывается прав, а проверка на такой таблице ничего не проверяет.
#include <string>

#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>
#include <userver/storages/postgres/query.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>
#include <userver/utest/utest.hpp>

#include "builders/identifiers.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/db/timestamps.hpp"
#include "scheduling_ground.hpp"
#include "scheduling_live_schema.hpp"

namespace pdr::scheduling::testing {
namespace {

constexpr int kTutors = 30;
constexpr int kDays = 365;

/// Репетитор и его ученик из наполнения. Номера те же, что раскладывает SQL
/// ниже, — иначе выборка пришлась бы на пустое место и план ничего бы не значил.
core::PersonId ACrowdedTutor() {
    return pdr::testing::Numbered<core::PersonId>(1000);
}

core::PersonId HisStudent() {
    return pdr::testing::Numbered<core::PersonId>(2000);
}

/// Год занятий тридцати репетиторам: по одному в день, встык не пересекаясь.
///
/// Наполнение идёт SQL-ом, а не доменом: десять тысяч занятий через `Save` —
/// это десять тысяч круговых ходов, и проверялась бы ими скорость вставки, а не
/// план выборки.
///
/// Заканчивается двумя действиями, без которых план был бы не тот. `ANALYZE`:
/// без свежей статистики планировщик считает таблицу пустой и уходит в Seq Scan
/// заслуженно. `SET LOCAL ROLE`: хозяин базы обходит RLS, и его план — не тот,
/// что будет в установке, где у прикладной роли к запросу добавляется условие
/// политики. Проверять надо именно его.
void Fill(infrastructure::db::ScopedTenantContext& scope) {
    scope.Session().Execute(
        "INSERT INTO scheduling_lesson (tenant_id, id, tutor_id, starts_at, ends_at, tz, state) "
        "SELECT $1::uuid, "
        "       ('00000000-0000-0000-0000-' || lpad((t * 1000 + d)::text, 12, '0'))::uuid, "
        "       ('00000000-0000-0000-0000-' || lpad(t::text, 12, '0'))::uuid, "
        "       timestamptz '2026-01-01 12:00:00+00' + make_interval(days => d), "
        "       timestamptz '2026-01-01 12:00:00+00' + make_interval(days => d) "
        "           + interval '1 hour', "
        "       'Europe/Moscow', "
        "       'planned' "
        "  FROM generate_series($2::int, $3::int) AS t, generate_series(0, $4::int) AS d",
        ContractGround::Tenant(),
        1000,
        1000 + kTutors - 1,
        kDays - 1);

    scope.Session().Execute(
        "INSERT INTO scheduling_lesson_participant (tenant_id, lesson_id, participant_id) "
        "SELECT $1::uuid, "
        "       ('00000000-0000-0000-0000-' || lpad((t * 1000 + d)::text, 12, '0'))::uuid, "
        "       ('00000000-0000-0000-0000-' || lpad((t + 1000)::text, 12, '0'))::uuid "
        "  FROM generate_series($2::int, $3::int) AS t, generate_series(0, $4::int) AS d",
        ContractGround::Tenant(),
        1000,
        1000 + kTutors - 1,
        kDays - 1);

    scope.Session().Execute("ANALYZE scheduling_lesson");
    scope.Session().Execute("ANALYZE scheduling_lesson_participant");

    scope.Session().Execute("SET LOCAL ROLE pdr_app");
}

/// Как Postgres собирается выполнять этот самый запрос — тот, что лежит в
/// db/sql, а не его пересказ в тесте.
///
/// Текст берётся у порождённого запроса, а не пишется рядом: пересказ разошёлся
/// бы с оригиналом на первой же правке, и план подтверждал бы индекс у запроса,
/// которого в дереве нет.
template<class... Args>
std::string PlanOf(infrastructure::db::ScopedTenantContext& scope,
                   const userver::storages::postgres::Query& query,
                   const Args&... args) {
    std::string plan;
    for (const auto& row :
         scope.Session().Execute("EXPLAIN " + std::string{query.GetStatementView()}, args...)) {
        plan += row.template As<std::string>();
        plan += "\n";
    }
    return plan;
}

}  // namespace

UTEST(SchedulingLessonPlan, AMonthOfTutorLessonsGoesThroughItsIndex) {
    userver::storages::postgres::utest::ClusterLocal local;
    ApplySchedulingSchema(local.GetCluster());
    infrastructure::db::TenantContext tenants{local.GetCluster()};

    auto scope = tenants.Open(ContractGround::Tenant(),
                              userver::storages::postgres::ClusterHostType::kMaster,
                              userver::storages::postgres::TransactionOptions{});
    Fill(scope);

    const auto plan = PlanOf(scope,
                             sql::kSchedulingLessonOfTutor,
                             ContractGround::Tenant(),
                             ACrowdedTutor(),
                             infrastructure::db::AsTimestamptz(ContractGround::Utc(2026, 3, 1, 0)),
                             infrastructure::db::AsTimestamptz(ContractGround::Utc(2026, 4, 1, 0)));

    EXPECT_NE(plan.find("scheduling_lesson_by_tutor"), std::string::npos)
        << "месяц занятий репетитора идёт мимо своего индекса:\n"
        << plan;
    EXPECT_EQ(plan.find("Seq Scan"), std::string::npos) << plan;
}

/// Два индекса на один запрос — это и есть замена отсутствующей колонки
/// starts_at в таблице участников: по участнику отбираются занятия, по
/// первичному ключу занятия достаётся сама строка. Вторая копия начала занятия
/// разошлась бы с первой молча, а два индекса — не расходятся.
UTEST(SchedulingLessonPlan, AMonthOfParticipantLessonsGoesThroughItsIndex) {
    userver::storages::postgres::utest::ClusterLocal local;
    ApplySchedulingSchema(local.GetCluster());
    infrastructure::db::TenantContext tenants{local.GetCluster()};

    auto scope = tenants.Open(ContractGround::Tenant(),
                              userver::storages::postgres::ClusterHostType::kMaster,
                              userver::storages::postgres::TransactionOptions{});
    Fill(scope);

    const auto plan = PlanOf(scope,
                             sql::kSchedulingLessonOfParticipant,
                             ContractGround::Tenant(),
                             HisStudent(),
                             infrastructure::db::AsTimestamptz(ContractGround::Utc(2026, 3, 1, 0)),
                             infrastructure::db::AsTimestamptz(ContractGround::Utc(2026, 4, 1, 0)));

    EXPECT_NE(plan.find("scheduling_lesson_by_participant"), std::string::npos)
        << "месяц занятий участника идёт мимо своего индекса:\n"
        << plan;
    EXPECT_NE(plan.find("scheduling_lesson_pk"), std::string::npos)
        << "занятие достаётся не по первичному ключу:\n"
        << plan;
    EXPECT_EQ(plan.find("Seq Scan"), std::string::npos) << plan;
}

}  // namespace pdr::scheduling::testing
