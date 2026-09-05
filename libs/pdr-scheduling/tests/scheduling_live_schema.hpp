#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>

/// @file
/// Схема расписания на живой базе — из НАСТОЯЩЕЙ миграции, а не из копии.
///
/// Вторая копия схемы расходится с первой в тот день, когда правку внесли в
/// одну, и прогон остаётся зелёным на схеме, которой в установке нет. Поэтому
/// живые проверки читают db/migrations и применяют её как есть.
namespace pdr::scheduling::testing {

/// `Cluster::Execute` готовит запрос, а готовый запрос состоит ровно из одной
/// команды, — поэтому файл разбирается на команды. Разбор простой намеренно: в
/// V013 нет ни долларовых кавычек, ни точек с запятой внутри строк, и
/// усложнять его до настоящего разборщика значило бы заводить вторую
/// применялку миграций.
inline std::vector<std::string> StatementsOfSchedulingMigration() {
    std::ifstream file{std::string{PDR_SOURCE_DIR} + "/db/migrations/V013__scheduling.sql"};
    std::stringstream whole;
    whole << file.rdbuf();

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
                     "DROP TABLE IF EXISTS scheduling_series_exception, "
                     "scheduling_series_participant, scheduling_series, "
                     "scheduling_lesson_participant, scheduling_lesson, "
                     "scheduling_availability_exception, scheduling_availability CASCADE");
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = "
                     "'pdr_app') THEN CREATE ROLE pdr_app NOLOGIN NOBYPASSRLS; END IF; END $$");
    for (const auto& statement : StatementsOfSchedulingMigration()) {
        if (BlankStatement(statement)) {
            continue;
        }
        cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster, statement);
    }
}

}  // namespace pdr::scheduling::testing
