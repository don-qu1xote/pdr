#pragma once

#include <cstdint>
#include <string_view>
#include <utility>

#include <userver/storages/postgres/cluster.hpp>
#include <userver/storages/postgres/query.hpp>

namespace pdr::infrastructure::db {

/// Зачем понадобился доступ в обход арендатора. СПИСОК ЗАКРЫТ.
///
/// Перечисление, а не строка с пояснением: строку можно написать какую угодно,
/// а новая причина здесь — это правка общего заголовка, которую видно в диффе и
/// на которую спросят «а точно?». Каждое значение ниже описано поимённо, и
/// шестого не бывает без обсуждения.
enum class UnscopedReason : std::uint8_t {
    /// Блокировка одиночного задания на весь кластер (`jobs_lock`). У неё нет
    /// арендатора по существу: она про то, кто из воркеров работает, а не о
    /// чьих данных идёт речь.
    kClusterWideJobLock,

    /// Журнал прогонов заданий (`jobs_run`). Таблица механизма, не предметной
    /// области: у неё нет колонки арендатора, и политике не на что смотреть.
    kClusterWideJobJournal,

    /// Применение миграций. Схему меняет не сервис, а `scripts/migrate.py`, и
    /// значение здесь заведено для полноты списка: DDL арендатора не знает.
    kSchemaMigration,

    /// Выгрузка оператора: обезличенный поток продуктовых событий целиком.
    /// Читать его сквозь арендаторов может только тот, кто и так может всё, —
    /// именно поэтому в нём нет идентификатора человека
    /// (docs/architecture/product-events.md).
    kOperatorExport,
};

std::string_view Name(UnscopedReason reason) noexcept;

/// ДОСТУП В ОБХОД АРЕНДАТОРА. Имя нарочно подозрительное.
///
/// Всё, что читает или пишет доменные данные, идёт через `TenantContext`. Этот
/// класс — вторая и последняя дверь, и она для работ, у которых арендатора не
/// бывает ПО СУЩЕСТВУ: блокировка на кластер, журнал прогонов, миграции,
/// выгрузка оператора.
///
/// Причина требуется конструктором и берётся из закрытого перечня выше.
/// «Мне только посмотреть» здесь не выражается: такого значения в перечислении
/// нет, а завести его — правка этого файла, которую увидят.
///
/// ЧЕГО ЗДЕСЬ НЕ ДЕЛАЮТ. Ни одна таблица с колонкой `tenant_id` через эту дверь
/// не читается и не пишется. Если понадобилось — значит, нужен `TenantContext`,
/// а не новое значение перечисления.
class UnscopedAccess final {
public:
    UnscopedAccess(userver::storages::postgres::ClusterPtr cluster, UnscopedReason reason)
        : cluster_{std::move(cluster)}, reason_{reason} {}

    UnscopedAccess(const UnscopedAccess&) = delete;
    UnscopedAccess& operator=(const UnscopedAccess&) = delete;

    UnscopedReason Reason() const noexcept {
        return reason_;
    }

    /// Пул целиком — для штатных механизмов userver, которым нужен именно он
    /// (`storages::postgres::DistLockStrategy`).
    const userver::storages::postgres::ClusterPtr& Pool() const noexcept {
        return cluster_;
    }

    /// Запрос на мастере. Обёртка нужна затем, чтобы вызывающий не называл ни
    /// пул, ни тип узла: тогда упоминание пула вне `infrastructure/db/`
    /// остаётся признаком обхода, а не обычным словом.
    template<class... Args>
    auto Execute(const userver::storages::postgres::Query& query, Args&&... args) const {
        return cluster_->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                                 query,
                                 std::forward<Args>(args)...);
    }

private:
    userver::storages::postgres::ClusterPtr cluster_;
    UnscopedReason reason_;
};

}  // namespace pdr::infrastructure::db
