#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/db/domain_types.hpp"

/// @file
/// Схема расписания на живой базе — из НАСТОЯЩЕЙ миграции, а не из копии.
///
/// Вторая копия схемы расходится с первой в тот день, когда правку внесли в
/// одну, и прогон остаётся зелёным на схеме, которой в установке нет. Поэтому
/// живые проверки читают db/migrations и применяют её как есть.
namespace pdr::scheduling::testing {

/// `Cluster::Execute` готовит запрос, а готовый запрос состоит ровно из одной
/// команды, — поэтому файл разбирается на команды. Разбор простой намеренно: в
/// миграциях расписания нет ни долларовых кавычек, ни точек с запятой внутри
/// строк, и усложнять его до настоящего разборщика значило бы заводить вторую
/// применялку миграций.
///
/// Миграций уже четыре, и читаются они ПО ПОРЯДКУ: история занятия ссылается на
/// само занятие внешним ключом, а участие обрастает колонками той таблицы,
/// которую завела первая, — в обратном порядке схема не создаётся.
inline std::vector<std::string> StatementsOfSchedulingMigration() {
    std::stringstream whole;
    for (const auto* name : {"/db/migrations/V013__scheduling.sql",
                             "/db/migrations/V014__lesson_history.sql",
                             "/db/migrations/V016__booking_window.sql",
                             "/db/migrations/V017__participation.sql"}) {
        std::ifstream file{std::string{PDR_SOURCE_DIR} + name};
        whole << file.rdbuf() << ";\n";
    }

    std::string text;
    std::string line;
    std::istringstream lines{whole.str()};
    while (std::getline(lines, line)) {
        const auto comment = line.find("--");
        text += (comment == std::string::npos ? line : line.substr(0, comment));
        text += "\n";
    }

    std::vector<std::string> statements;
    std::string current;
    for (const char symbol : text) {
        if (symbol == ';') {
            statements.push_back(current);
            current.clear();
            continue;
        }
        current += symbol;
    }
    return statements;
}

inline bool BlankStatement(const std::string& statement) {
    return statement.find_first_not_of(" \t\r\n") == std::string::npos;
}

/// Разложить схему расписания на чистом месте.
///
/// Роль `pdr_app` заводится здесь же: миграция раздаёт ей права, а вне установки
/// её никто не создавал.
inline void ApplySchedulingSchema(const userver::storages::postgres::ClusterPtr& cluster) {
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "DROP TABLE IF EXISTS scheduling_booking_window, scheduling_lesson_history, "
                     "scheduling_series_exception, "
                     "scheduling_series_participant, scheduling_series, "
                     "scheduling_lesson_participant, scheduling_lesson, "
                     "scheduling_availability_exception, scheduling_availability CASCADE");
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = "
                     "'pdr_app') THEN CREATE ROLE pdr_app NOLOGIN NOBYPASSRLS; END IF; END $$");

    /// Арендатор и люди — своими таблицами: послабления окон ссылаются на них
    /// внешними ключами, а применять сюда V002 целиком значило бы стирать чужие
    /// живые проверки, которые те же таблицы заполняют.
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "CREATE TABLE IF NOT EXISTS identity_tenant ("
                     "    tenant_id  uuid        not null primary key,"
                     "    name       text        not null,"
                     "    tz         text        not null,"
                     "    created_at timestamptz not null default now())");
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "CREATE TABLE IF NOT EXISTS identity_person ("
                     "    tenant_id    uuid not null references identity_tenant (tenant_id),"
                     "    id           uuid not null,"
                     "    display_name text not null,"
                     "    tz           text not null,"
                     "    constraint identity_person_pk primary key (tenant_id, id))");

    for (const auto& statement : StatementsOfSchedulingMigration()) {
        if (BlankStatement(statement)) {
            continue;
        }
        cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster, statement);
    }
}

/// Завести практику, от имени которой выдают послабления.
inline void OpenPractice(const userver::storages::postgres::ClusterPtr& cluster,
                         const core::TenantId& tenant) {
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "INSERT INTO identity_tenant (tenant_id, name, tz) "
                     "VALUES ($1, 'практика', 'Europe/Moscow') ON CONFLICT DO NOTHING",
                     tenant);
}

}  // namespace pdr::scheduling::testing
