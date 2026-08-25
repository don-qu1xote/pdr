#include "infrastructure/postgres_tenant_aware_repository.hpp"

#include <utility>

#include <userver/storages/postgres/query.hpp>

namespace pdr::infrastructure {
namespace {

/// Третий аргумент — `true`: объявление живёт до конца ТРАНЗАКЦИИ, а не до
/// конца соединения. Это не мелочь и не оптимизация. Соединение возвращается в
/// пул и достаётся следующему запросу; объявление, пережившее транзакцию,
/// приехало бы вместе с ним — и следующий арендатор увидел бы предыдущего.
///
/// Имя параметра то же, что в политиках миграции; их совпадение проверяет
/// scripts/check_rls.py, потому что опечатка здесь — это пустые ответы во всей
/// системе, и выглядит она как «данные пропали», а не как ошибка.
const userver::storages::postgres::Query kDeclareTenant{
    "SELECT set_config('pdr.tenant_id', $1, true)",
    userver::storages::postgres::Query::Name{"declare_tenant"},
};

}  // namespace

PostgresTenantAwareRepository::PostgresTenantAwareRepository(
    userver::storages::postgres::ClusterPtr cluster)
    : cluster_{std::move(cluster)} {}

void PostgresTenantAwareRepository::Run(const core::TenantId& tenant, const Work& work) {
    auto transaction = cluster_->Begin(userver::storages::postgres::ClusterHostType::kMaster,
                                       userver::storages::postgres::TransactionOptions{});
    transaction.Execute(kDeclareTenant, tenant.ToString());

    work(transaction);

    transaction.Commit();
}

}  // namespace pdr::infrastructure
