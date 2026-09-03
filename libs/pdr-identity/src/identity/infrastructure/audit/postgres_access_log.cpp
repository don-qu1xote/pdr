#include "identity/infrastructure/audit/postgres_access_log.hpp"

#include <string>

#include <pdr/sql_queries.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

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
                             record.Tenant(),
                             ids_.Next<RowId>(),
                             record.Actor(),
                             record.Subject(),
                             Name(record.Kind()),
                             record.At());
}

}  // namespace pdr::identity
