#include "identity/infrastructure/audit/postgres_access_log.hpp"

#include <string>

#include <pdr/sql_queries.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsTimestamptz;

/// Идентификатор строки журнала. Метки в общем списке `core/types/ids.hpp` для
/// него нет намеренно: на строку журнала не ссылается никто — ни домен, ни
/// другая таблица. Она нужна базе, чтобы у строки был первичный ключ, и живёт
/// ровно здесь.
using RowId = core::StrongId<struct AccessLogRowTag>;

}  // namespace

PostgresAccessLog::PostgresAccessLog(infrastructure::db::ScopedTenantContext& scope,
                                     const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

void PostgresAccessLog::Record(const AccessRecord& record) {
    scope_.Session().Execute(sql::kIdentityAccessLogRecord,
                             record.Tenant().ToString(),
                             ids_.Next<RowId>().ToString(),
                             record.Actor().ToString(),
                             record.Subject().ToString(),
                             std::string{Name(record.Kind())},
                             AsTimestamptz(record.At()));
}

}  // namespace pdr::identity
