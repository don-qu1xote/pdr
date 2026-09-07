#pragma once

#include <optional>

#include "core/digest.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "scheduling/application/ports/calendar_feeds.hpp"

namespace pdr::scheduling {

/// Подписки на календарь в Postgres.
///
/// В БАЗЕ САМОГО СЕКРЕТА НЕТ — только отпечаток. Отсюда и форма поиска: пришло
/// значение из адреса, посчитали отпечаток, нашли строку. Сравнить пришедшее с
/// хранимым отпечаток позволяет, восстановить из него секрет — нет.
///
/// Выдача и перевыпуск — одно обращение (`on conflict do update`): утёкшую
/// ссылку чинят одним нажатием, и промежутка, в котором ленты нет ни старой, ни
/// новой, не бывает.
class PostgresCalendarFeeds final : public ports::CalendarFeeds {
public:
    explicit PostgresCalendarFeeds(infrastructure::db::ScopedTenantContext& scope) noexcept;

    core::Result<void> Issue(const CalendarFeed& feed, core::Instant issued_at) override;

    std::optional<CalendarFeed> ByDigest(const core::TenantId& tenant,
                                         const core::Digest& digest) const override;

    std::optional<CalendarNaming> NamingOf(const core::TenantId& tenant,
                                           const core::PersonId& person) const override;

    core::Result<void> Rename(const core::TenantId& tenant,
                              const core::PersonId& person,
                              CalendarNaming naming) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::scheduling
