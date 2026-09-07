#pragma once

#include "application/ports/time_zone_rules.hpp"
#include "core/errors.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::infrastructure::db {

/// Правила зоны из базы IANA, которая уже стоит в Postgres.
///
/// ЕДИНСТВЕННЫЙ АДАПТЕР В ДЕРЕВЕ, КОТОРЫЙ ХОДИТ В БАЗУ НЕ ЗА НАШИМИ ДАННЫМИ, и
/// это не обходной путь: таблица переводов часов — тоже данные, просто не наши.
/// Она есть у Postgres, обновляется вместе с ним и живёт в одном экземпляре на
/// установку; у ядра её нет намеренно, чтобы домен собирался и проверялся без
/// системы.
///
/// Область арендатора нужна только ради соединения: запрос не читает ни одной
/// таблицы, и политике изоляции нечего в нём проверять. Отдельного пути к базе
/// мимо области ради этого не заводится — он жил бы вне транзакции обращения и
/// первым же отказом это показал.
class PostgresTimeZoneRules final : public application::ports::TimeZoneRules {
public:
    explicit PostgresTimeZoneRules(ScopedTenantContext& scope) noexcept;

    core::Result<core::ZoneOffsets> For(const core::TimeZone& zone,
                                        const core::TimeRange& covering) const override;

private:
    ScopedTenantContext& scope_;
};

}  // namespace pdr::infrastructure::db
