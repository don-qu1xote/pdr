#include "identity/infrastructure/audit/postgres_access_log.hpp"

#include <chrono>
#include <string>

#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/query.hpp>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {
namespace {

using Timestamptz = userver::storages::postgres::TimePointTz;

/// Идентификатор строки журнала. Метки в общем списке `core/types/ids.hpp` для
/// него нет намеренно: на строку журнала не ссылается никто — ни домен, ни
/// другая таблица. Она нужна базе, чтобы у строки был первичный ключ, и живёт
/// ровно здесь.
using RowId = core::StrongId<struct AccessLogRowTag>;

Timestamptz AsTimestamptz(core::Instant instant) {
    return Timestamptz{userver::storages::postgres::TimePoint{
        std::chrono::duration_cast<userver::storages::postgres::TimePoint::duration>(
            std::chrono::microseconds{instant.UnixMicros()})}};
}

/// Приведение `::uuid` написано явно. Идентификатор уезжает текстом — тем же
/// `ToString()`, что и всюду, — а колонка типизирована; полагаться на то, что
/// база сама догадается привести текст к uuid при вставке, здесь не за чем:
/// написанное приведение видно, а угаданное — нет.
///
/// Момент приходит параметром, хотя у колонки есть `default now()`: часы у нас
/// портом, и строка «кто смотрел в марте» должна отвечать по тем же часам, что
/// и весь остальной сценарий, а не по вторым, базы.
const userver::storages::postgres::Query kRecord{
    "INSERT INTO identity_access_log (tenant_id, id, actor_id, subject_id, resource_kind, at) "
    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4::uuid, $5, $6)",
    userver::storages::postgres::Query::Name{"identity_access_log_record"},
};

}  // namespace

PostgresAccessLog::PostgresAccessLog(infrastructure::db::ScopedTenantContext& scope,
                                     const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

void PostgresAccessLog::Record(const AccessRecord& record) {
    scope_.Session().Execute(kRecord,
                             record.Tenant().ToString(),
                             ids_.Next<RowId>().ToString(),
                             record.Actor().ToString(),
                             record.Subject().ToString(),
                             std::string{Name(record.Kind())},
                             AsTimestamptz(record.At()));
}

}  // namespace pdr::identity
