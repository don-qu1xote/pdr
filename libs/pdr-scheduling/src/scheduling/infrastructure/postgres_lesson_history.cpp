#include "scheduling/infrastructure/postgres_lesson_history.hpp"

#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"
#include "infrastructure/db/timestamps.hpp"

namespace pdr::scheduling {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

core::PersonId AsPerson(const std::string& text) {
    const auto parsed = core::PersonId::Parse(text);
    if (!parsed.has_value()) {
        throw std::runtime_error{"scheduling_lesson_history.actor_id не идентификатор человека"};
    }
    return *parsed;
}

LessonAction AsAction(const std::string& text) {
    for (const auto action : kEveryLessonAction) {
        if (Name(action) == text) {
            return action;
        }
    }
    throw std::runtime_error{"scheduling_lesson_history.action вне закрытого списка: " + text};
}

}  // namespace

PostgresLessonHistory::PostgresLessonHistory(infrastructure::db::ScopedTenantContext& scope,
                                             const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

core::Result<void> PostgresLessonHistory::Record(const LessonHistoryEntry& entry) {
    scope_.Session().Execute(sql::kSchedulingLessonHistoryAdd,
                             entry.tenant,
                             ids_.Next<LessonHistoryId>(),
                             entry.lesson,
                             entry.actor,
                             std::string{Name(entry.action)},
                             entry.at,
                             entry.details);
    return {};
}

std::vector<LessonHistoryEntry> PostgresLessonHistory::Of(const core::TenantId& tenant,
                                                          const core::LessonId& lesson) const {
    const auto rows = scope_.Session().Execute(sql::kSchedulingLessonHistoryOf, tenant, lesson);

    std::vector<LessonHistoryEntry> found;
    found.reserve(rows.Size());
    for (const auto& raw : rows) {
        const auto row = raw.As<SchedulingLessonHistoryOfRow>(userver::storages::postgres::kRowTag);
        found.push_back(LessonHistoryEntry{tenant,
                                           lesson,
                                           AsPerson(Filled(row.actor_id, "actor_id")),
                                           AsAction(Filled(row.action, "action")),
                                           AsInstant(Filled(row.at, "at")),
                                           Filled(row.details, "details")});
    }
    return found;
}

}  // namespace pdr::scheduling
