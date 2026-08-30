#include "infrastructure/db/tenant_context.hpp"

#include <userver/storages/postgres/query.hpp>

namespace pdr::infrastructure::db {
namespace {

/// ЕДИНСТВЕННОЕ МЕСТО ВО ВСЁМ ДЕРЕВЕ, где арендатор объявляется базе.
///
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

TenantContext::TenantContext(userver::storages::postgres::ClusterPtr cluster)
    : cluster_{std::move(cluster)} {}

ScopedTenantContext TenantContext::Open(const core::TenantId& tenant) {
    auto transaction = cluster_->Begin(userver::storages::postgres::ClusterHostType::kMaster,
                                       userver::storages::postgres::TransactionOptions{});
    transaction.Execute(kDeclareTenant, tenant.ToString());

    return ScopedTenantContext{std::move(transaction), tenant};
}

}  // namespace pdr::infrastructure::db
