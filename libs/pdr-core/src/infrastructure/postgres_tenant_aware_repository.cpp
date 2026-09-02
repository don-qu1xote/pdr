#include "infrastructure/postgres_tenant_aware_repository.hpp"

#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>

namespace pdr::infrastructure {
namespace {

using Intent = application::ports::Intent;

/// ЕДИНСТВЕННОЕ МЕСТО, где намерение превращается в хост и режим транзакции.
///
/// `kSlaveOrMaster`, а не `kSlave`: реплики может не быть вовсе — в установке
/// её и нет, — и чтение обязано состояться, а не отказать за неимением
/// реплики. Флаг говорит «лучше реплика», а не «только реплика».
userver::storages::postgres::ClusterHostTypeFlags HostFor(Intent intent) noexcept {
    return intent == Intent::kReading ? userver::storages::postgres::ClusterHostType::kSlaveOrMaster
                                      : userver::storages::postgres::ClusterHostType::kMaster;
}

/// Читающая транзакция — не оптимизация, а заявление базе: запись внутри неё
/// отказывает. Работа, объявившая себя читающей и всё-таки написавшая, падает
/// сразу и на своём запросе, а не через полгода на реплике.
userver::storages::postgres::TransactionOptions ModeFor(Intent intent) noexcept {
    return intent == Intent::kReading
               ? userver::storages::postgres::
                     TransactionOptions{userver::storages::postgres::TransactionOptions::kReadOnly}
               : userver::storages::postgres::TransactionOptions{};
}

}  // namespace

PostgresTenantAwareRepository::PostgresTenantAwareRepository(db::TenantContext& context) noexcept
    : context_{context} {}

void PostgresTenantAwareRepository::Run(application::ports::Intent intent,
                                        const core::TenantId& tenant,
                                        const Work& work) {
    auto scope = context_.Open(tenant, HostFor(intent), ModeFor(intent));

    work(scope);

    scope.Commit();
}

}  // namespace pdr::infrastructure
