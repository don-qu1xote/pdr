/// @file
/// ВТОРАЯ ИНСТАНЦИАЦИЯ contract-набора хранилища — против настоящего адаптера.
///
/// Первая гоняется на фейке и не требует ничего (libs/pdr-testing/tests). Эта
/// требует живой базы, и до появления процесса её негде было запустить: набор
/// написан на сопрограммах userver, а поднимать их было нечем.
///
/// Четыре макроса ниже переопределяются ДО включения набора: тогда те же самые
/// утверждения разворачиваются в UTEST-макросы и идут внутри сопрограммы. Иначе
/// пришлось бы держать вторую копию проверок, а две копии расходятся в первый
/// же день, когда правку внесли в одну.
#include <userver/utest/utest.hpp>

#define PDR_CONTRACT_SUITE_P(suite) TYPED_UTEST_SUITE_P(suite)
#define PDR_CONTRACT_TEST_P(suite, name) TYPED_UTEST_P(suite, name)
#define PDR_CONTRACT_REGISTER_P(suite, ...) REGISTER_TYPED_UTEST_SUITE_P(suite, __VA_ARGS__)
#define PDR_CONTRACT_INSTANTIATE_P(prefix, suite, types) \
    INSTANTIATE_TYPED_UTEST_SUITE_P(prefix, suite, types)

#include <array>
#include <cstddef>
#include <exception>
#include <string>
#include <vector>

#include <pdr/testing/repository_contract.hpp>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/utest/cluster_local.hpp>

#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/postgres_tenant_aware_repository.hpp"

namespace pdr::testing {
namespace {

/// РОЛЬ, КОТОРОЙ ПОЛИТИКА ПИСАНА.
///
/// Владелец базы в прогоне — суперпользователь, а построчная защита на него не
/// действует вовсе: прогон под ним был бы зелёным при выключенной политике.
/// Поэтому работа идёт под отдельной ролью без права обхода — той же по смыслу,
/// что pdr_app в установке.
constexpr std::string_view kRole = "pdr_contract_app";

/// Своя таблица, а не доменная: набор проверяет МЕХАНИЗМ области, и привязка
/// его к чужой схеме превратила бы падение набора в вопрос «а что там у
/// identity».
///
/// Запросы поодиночке, а не одной строкой: `Cluster::Execute` готовит запрос
/// (prepared statement), а такой запрос состоит ровно из одной команды.
constexpr std::array<std::string_view, 8> kSetUp{
    R"(do $$ begin
           if not exists (select 1 from pg_roles where rolname = 'pdr_contract_app') then
               create role pdr_contract_app nologin nobypassrls;
           end if;
       end $$)",
    "drop table if exists contract_row",
    "create table contract_row (tenant_id uuid not null, payload text not null)",
    "alter table contract_row enable row level security",
    "alter table contract_row force row level security",
    R"(create policy contract_row_isolation on contract_row
           using (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid)
           with check (tenant_id = nullif(current_setting('pdr.tenant_id', true), '')::uuid))",
    "grant select, insert, update, delete on contract_row to pdr_contract_app",
    "grant usage on schema public to pdr_contract_app",
};

/// Мир настоящего адаптера: как создать реализацию и как задать ей вопросы
/// набора на её языке.
struct PostgresRepositoryWorld final {
    using Session = infrastructure::db::ScopedTenantContext;

    PostgresRepositoryWorld() {
        for (const auto statement : kSetUp) {
            local_.GetCluster()->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                         std::string{statement});
        }
    }

    application::ports::TenantAwareRepository<Session>& Repository() noexcept {
        return repository_;
    }

    static void AsApplication(Session& session) {
        session.Session().Execute("SET LOCAL ROLE " + std::string{kRole});
    }

    static void Insert(Session& session, std::string payload) {
        AsApplication(session);
        session.Session().Execute(
            "INSERT INTO contract_row (tenant_id, payload) "
            "VALUES (nullif(current_setting('pdr.tenant_id', true), '')::uuid, $1)",
            payload);
    }

    /// ТОЧКА СОХРАНЕНИЯ ЗДЕСЬ — ЧАСТЬ ПРОВЕРКИ, А НЕ ОБХОД ЕЁ.
    ///
    /// Отказ политики в Postgres роняет транзакцию целиком: после него не
    /// проходит ни один запрос, включая тот, которым набор считает легшие
    /// строки. Точка сохранения — штатный способ пережить отказ внутри
    /// транзакции, и она ровно тем и занята: даёт набору дожить до вопроса
    /// «сколько строк написалось».
    ///
    /// В рабочем коде её здесь нет и не нужно: попытка написать чужую строку —
    /// это ошибка, и транзакции правильно кончиться. Разница между фейком и
    /// базой в этом месте настоящая, и она названа вслух, а не спрятана.
    static bool InsertFor(Session& session, const core::TenantId& owner, std::string payload) {
        AsApplication(session);
        session.Session().Execute("SAVEPOINT foreign_write");
        try {
            session.Session().Execute(
                "INSERT INTO contract_row (tenant_id, payload) VALUES ($1::uuid, $2)",
                owner.ToString(),
                payload);
        } catch (const std::exception&) {
            session.Session().Execute("ROLLBACK TO SAVEPOINT foreign_write");
            return false;
        }
        session.Session().Execute("RELEASE SAVEPOINT foreign_write");
        return true;
    }

    static std::vector<std::string> SelectAll(Session& session) {
        AsApplication(session);
        const auto rows = session.Session().Execute("SELECT payload FROM contract_row");

        std::vector<std::string> found;
        found.reserve(rows.Size());
        for (const auto& row : rows) {
            found.push_back(row["payload"].As<std::string>());
        }
        return found;
    }

    static std::size_t DeleteAll(Session& session) {
        AsApplication(session);
        return session.Session().Execute("DELETE FROM contract_row").RowsAffected();
    }

    /// Спрашивается У БАЗЫ, а не у области: набор проверяет, что арендатор
    /// объявлен соединению, а не что его помнит наш объект.
    static core::TenantId Declared(Session& session) {
        const auto declared =
            core::TenantId::Parse(session.Session()
                                      .Execute("SELECT current_setting('pdr.tenant_id', true)")
                                      .AsSingleRow<std::string>());
        return declared.value_or(core::TenantId::FromBytes(core::IdBytes{}));
    }

    std::size_t RowsBypassingPolicy() {
        return local_.GetCluster()
            ->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      "SELECT count(*) FROM contract_row")
            .AsSingleRow<std::int64_t>();
    }

private:
    userver::storages::postgres::utest::ClusterLocal local_;
    infrastructure::db::TenantContext tenants_{local_.GetCluster()};
    infrastructure::PostgresTenantAwareRepository repository_{tenants_};
};

}  // namespace

PDR_REPOSITORY_CONTRACT(Postgres, PostgresRepositoryWorld);

}  // namespace pdr::testing
