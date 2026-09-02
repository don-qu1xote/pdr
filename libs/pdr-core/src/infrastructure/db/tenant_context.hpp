#pragma once

#include <utility>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/transaction.hpp>

#include "core/types/ids.hpp"

namespace pdr::infrastructure::db {

/// Область, внутри которой арендатор объявлен базе.
///
/// Соединения без объявленного арендатора в этом дереве не существует — не
/// потому, что все помнят, а потому, что его НЕЧЕМ получить: единственный
/// конструктор закрыт, и открывает область только `TenantContext::Open`,
/// которому арендатор нужен параметром. Забыть его — не «ошибка внимательности»,
/// а несобирающийся код.
///
/// Объявление живёт до конца транзакции (`set_config(..., true)`), поэтому
/// возврат соединения в пул очищает его сам: своего try/finally здесь нет и не
/// нужно. Соединение, вернувшееся в пул с чужим арендатором, — классическая
/// утечка, и проверяется она на живой базе
/// (`scripts/check_isolation.py`, случай «параметр не переживает транзакцию»).
class ScopedTenantContext final {
public:
    ScopedTenantContext(const ScopedTenantContext&) = delete;
    ScopedTenantContext& operator=(const ScopedTenantContext&) = delete;
    ScopedTenantContext(ScopedTenantContext&&) = delete;
    ScopedTenantContext& operator=(ScopedTenantContext&&) = delete;

    ~ScopedTenantContext() = default;

    /// Сессия области: та самая транзакция, в которой уже объявлен арендатор.
    userver::storages::postgres::Transaction& Session() noexcept {
        return transaction_;
    }

    const core::TenantId& Tenant() const noexcept {
        return tenant_;
    }

    /// Закончить область успехом. Не позвали — транзакция откатится сама, и
    /// это тоже штатное поведение userver, а не наша договорённость.
    void Commit() {
        transaction_.Commit();
    }

private:
    friend class TenantContext;

    ScopedTenantContext(userver::storages::postgres::Transaction transaction, core::TenantId tenant)
        : transaction_{std::move(transaction)}, tenant_{std::move(tenant)} {}

    userver::storages::postgres::Transaction transaction_;
    core::TenantId tenant_;
};

/// Единственная дверь к пулу соединений для доменных данных.
///
/// Репозитории контекстов конструируются от области (`ScopedTenantContext&`), а
/// не от пула: пул в их заголовках не упоминается вовсе, и «сходить в базу
/// мимо арендатора» им нечем. Это правило проверяется машиной —
/// `scripts/check_layers.py` роняет сборку на упоминании пула вне
/// `infrastructure/db/`.
///
/// Второй двери нет ровно одна: `UnscopedAccess` для работ, у которых
/// арендатора не бывает по существу. Она названа так, чтобы её было видно на
/// ревью.
class TenantContext final {
public:
    explicit TenantContext(userver::storages::postgres::ClusterPtr cluster);

    TenantContext(const TenantContext&) = delete;
    TenantContext& operator=(const TenantContext&) = delete;

    /// Взять соединение и объявить арендатора ДО первого запроса.
    ///
    /// ХОСТ И РЕЖИМ ТРАНЗАКЦИИ — ПАРАМЕТРЫ, А НЕ РЕШЕНИЕ АДАПТЕРА. «Пойдёт ли
    /// этот запрос на реплику» — свойство сценария, а не соединения: адаптер
    /// видит только «дайте область» и, решая сам, решал бы за всех одинаково.
    /// Так и было — мастер и пишущая транзакция на каждое чтение, — и оттого
    /// чтение занимало соединение мастера и держало пишущую транзакцию, которой
    /// нечего писать.
    ///
    /// Транзакция нужна ОБОИМ путям и читающему тоже: объявление арендатора
    /// живёт до её конца (`set_config(..., true)`), и без транзакции его нечем
    /// ограничить — оно уехало бы в пул вместе с соединением.
    ScopedTenantContext Open(const core::TenantId& tenant,
                             userver::storages::postgres::ClusterHostTypeFlags host,
                             const userver::storages::postgres::TransactionOptions& options);

private:
    userver::storages::postgres::ClusterPtr cluster_;
};

}  // namespace pdr::infrastructure::db
