#pragma once

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/cluster_types.hpp>

/// @file
/// Очередь на живой базе — из НАСТОЯЩЕЙ миграции, а не из копии.
///
/// Вторая копия схемы расходится с первой в тот день, когда правку внесли в
/// одну, и прогон остаётся зелёным на схеме, которой в установке нет. А здесь
/// проверяется именно схема: обе политики, ограничение причины отказа и
/// уникальность ключа намерения — то, чего у фейка нет вовсе.
namespace pdr::notifications::testing {

/// Разбор простой намеренно: в этой миграции нет ни долларовых кавычек, ни
/// точек с запятой внутри строк, а настоящая применялка миграций уже написана и
/// заводить вторую незачем.
inline std::vector<std::string> StatementsOfOutboxMigration() {
    std::stringstream whole;
    std::ifstream file{std::string{PDR_SOURCE_DIR} + "/db/migrations/V015__outbox.sql"};
    whole << file.rdbuf() << ";\n";

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

/// Разложить очередь на чистом месте.
///
/// Арендатор заводится своей таблицей: миграция очереди ссылается на неё внешним
/// ключом, а применять сюда V002 целиком значило бы стирать чужие живые
/// проверки, которые ту же таблицу заполняют. Роль `pdr_app` — здесь же:
/// миграция раздаёт ей права, а вне установки её никто не создавал.
inline void ApplyOutboxSchema(const userver::storages::postgres::ClusterPtr& cluster) {
    const auto run = [&cluster](const std::string& statement) {
        cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster, statement);
    };

    run("DROP TABLE IF EXISTS notifications_outbox CASCADE");
    run("DO $$ BEGIN IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'pdr_app') "
        "THEN CREATE ROLE pdr_app NOLOGIN NOBYPASSRLS; END IF; END $$");
    run("CREATE TABLE IF NOT EXISTS identity_tenant ("
        "    tenant_id  uuid        not null primary key,"
        "    name       text        not null,"
        "    tz         text        not null,"
        "    created_at timestamptz not null default now())");

    for (const auto& statement : StatementsOfOutboxMigration()) {
        if (BlankStatement(statement)) {
            continue;
        }
        run(statement);
    }
}

/// Завести практику, от имени которой кладут письма.
inline void OpenPractice(const userver::storages::postgres::ClusterPtr& cluster,
                         const std::string& tenant) {
    cluster->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                     "INSERT INTO identity_tenant (tenant_id, name, tz) "
                     "VALUES ($1::uuid, 'практика', 'Europe/Moscow') "
                     "ON CONFLICT DO NOTHING",
                     tenant);
}

}  // namespace pdr::notifications::testing
