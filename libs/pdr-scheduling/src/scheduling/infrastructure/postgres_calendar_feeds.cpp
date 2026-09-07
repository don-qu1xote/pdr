#include "scheduling/infrastructure/postgres_calendar_feeds.hpp"

#include <optional>
#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::Filled;

CalendarNaming AsNaming(const std::string& text) {
    const auto naming = ParseCalendarNaming(text);
    if (!naming.has_value()) {
        throw std::runtime_error{"scheduling_calendar_feed.naming вне закрытого списка: " + text};
    }
    return *naming;
}

}  // namespace

PostgresCalendarFeeds::PostgresCalendarFeeds(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

core::Result<void> PostgresCalendarFeeds::Issue(const CalendarFeed& feed, core::Instant issued_at) {
    scope_.Session().Execute(sql::kSchedulingCalendarFeedIssue,
                             feed.Tenant(),
                             feed.Person(),
                             feed.Secret().Value(),
                             std::string{Name(feed.Naming())},
                             issued_at);
    return {};
}

std::optional<CalendarFeed> PostgresCalendarFeeds::ByDigest(const core::TenantId& tenant,
                                                            const core::Digest& digest) const {
    const auto found =
        scope_.Session().Execute(sql::kSchedulingCalendarFeedByDigest, tenant, digest.Value());
    if (found.IsEmpty()) {
        return std::nullopt;
    }

    const auto row =
        found.Front().As<SchedulingCalendarFeedByDigestRow>(userver::storages::postgres::kRowTag);

    const auto person = core::PersonId::Parse(Filled(row.person_id, "person_id"));
    if (!person.has_value()) {
        throw std::runtime_error{"scheduling_calendar_feed.person_id не идентификатор человека"};
    }

    auto feed =
        CalendarFeed::Compose(tenant, *person, digest, AsNaming(Filled(row.naming, "naming")));
    if (!feed.HasValue()) {
        throw std::runtime_error{"строка scheduling_calendar_feed не собирается в подписку: " +
                                 feed.Failure().Code()};
    }
    return feed.Value();
}

std::optional<CalendarNaming> PostgresCalendarFeeds::NamingOf(const core::TenantId& tenant,
                                                              const core::PersonId& person) const {
    const auto found =
        scope_.Session().Execute(sql::kSchedulingCalendarFeedOfPerson, tenant, person);
    if (found.IsEmpty()) {
        return std::nullopt;
    }

    /// Колонка одна, и структуры строки под неё не порождается: читается она
    /// значением — тем же способом, что и остальные однополосные выборки дерева.
    return AsNaming(Filled(found.Front().As<std::optional<std::string>>(), "naming"));
}

core::Result<void> PostgresCalendarFeeds::Rename(const core::TenantId& tenant,
                                                 const core::PersonId& person,
                                                 CalendarNaming naming) {
    const auto written = scope_.Session().Execute(
        sql::kSchedulingCalendarFeedNaming, tenant, person, std::string{Name(naming)});
    if (written.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kNotFound,
                           "calendar_feed_not_found",
                           "такой подписки на календарь здесь нет"};
    }
    return {};
}

}  // namespace pdr::scheduling
