#pragma once

#include "core/errors.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/local_days.hpp"

namespace pdr::scheduling {

/// МЕСТНЫЕ ДНИ В МОМЕНТЫ — СЧИТАЕТ POSTGRES.
///
/// Единственный адаптер в дереве, который ходит в базу НЕ ЗА ДАННЫМИ. Это не
/// странность и не обходной путь: таблица переводов часов — тоже данные, просто
/// не наши. Она есть у Postgres, обновляется вместе с ним и живёт в одном
/// экземпляре на установку; у ядра её нет намеренно, чтобы домен собирался и
/// проверялся без системы (`core::ZoneOffsets` приходит к нему значением).
///
/// Альтернатива — своя копия базы IANA в процессе — стоила бы ровно того же
/// ответа плюс обязанность её обновлять, и первое же расхождение с базой
/// пришлось бы искать в двух местах сразу.
///
/// ОБЛАСТЬ АРЕНДАТОРА ЗДЕСЬ НУЖНА ТОЛЬКО РАДИ СОЕДИНЕНИЯ. Запрос не читает ни
/// одной таблицы, и политике изоляции нечего в нём проверять; отдельного пути к
/// базе мимо области ради этого не заводится — он бы жил вне транзакции
/// обращения и первым же отказом это показал.
class PostgresLocalDays final : public ports::LocalDays {
public:
    explicit PostgresLocalDays(infrastructure::db::ScopedTenantContext& scope) noexcept;

    core::Result<core::TimeRange> Between(const core::Date& from,
                                          const core::Date& to,
                                          const core::TimeZone& zone) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
