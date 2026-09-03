#include "identity/infrastructure/auth/postgres_login_attempts.hpp"

#include <cstdint>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

/// Оба запроса — и учёт попытки, и её чтение — отдают одну пару колонок, и
/// разбирается она одной структурой. Своей у `identity_login_attempt_register`
/// нет: вставку с образцовыми значениями разборщик схемы не выполняет
/// (`@no-dto` в файле запроса и причина там же). Разъедься состав возвращаемого
/// у этих двух запросов — разбор упадёт на несовпадении числа колонок, потому
/// что `kRowTag` его сверяет.
AttemptWindow From(const IdentityLoginAttemptSeenRow& row) {
    return AttemptWindow::Restore(AsInstant(Filled(row.window_started_at, "window_started_at")),
                                  Filled(row.attempts, "attempts"));
}

}  // namespace

PostgresLoginAttempts::PostgresLoginAttempts(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

AttemptWindow PostgresLoginAttempts::Register(const core::TenantId& tenant,
                                              AttemptSubject subject,
                                              const Digest& of,
                                              core::Instant at,
                                              core::Instant::Duration window) {
    const auto result = scope_.Session().Execute(
        sql::kIdentityLoginAttemptRegister, tenant, Name(subject), of.Value(), at, at - window);
    return From(
        result.Front().As<IdentityLoginAttemptSeenRow>(userver::storages::postgres::kRowTag));
}

AttemptWindow PostgresLoginAttempts::Seen(const core::TenantId&,
                                          AttemptSubject subject,
                                          const Digest& of) const {
    const auto result =
        scope_.Session().Execute(sql::kIdentityLoginAttemptSeen, Name(subject), of.Value());
    if (result.IsEmpty()) {
        return AttemptWindow::Restore(core::Instant::FromUnixMicros(0), 0);
    }

    return From(
        result.Front().As<IdentityLoginAttemptSeenRow>(userver::storages::postgres::kRowTag));
}

void PostgresLoginAttempts::Forget(const core::TenantId&,
                                   AttemptSubject subject,
                                   const Digest& of) {
    scope_.Session().Execute(sql::kIdentityLoginAttemptForget, Name(subject), of.Value());
}

}  // namespace pdr::identity
