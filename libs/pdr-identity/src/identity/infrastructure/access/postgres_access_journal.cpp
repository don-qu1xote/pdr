#include "identity/infrastructure/access/postgres_access_journal.hpp"

#include <stdexcept>
#include <string>

#include <userver/storages/postgres/query.hpp>

#include "infrastructure/db/timestamps.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Timestamptz;

const userver::storages::postgres::Query kAboutPerson{
    "SELECT actor_id, resource_kind, outcome, at FROM identity_access_log "
    "WHERE subject_id = $1::uuid AND at >= $2 ORDER BY at DESC",
    userver::storages::postgres::Query::Name{"identity_access_log_about_person"},
};

}  // namespace

PostgresAccessJournal::PostgresAccessJournal(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::vector<AccessRecord> PostgresAccessJournal::AboutPerson(const core::TenantId& tenant,
                                                             const core::PersonId& subject,
                                                             core::Instant since) const {
    const auto result =
        scope_.Session().Execute(kAboutPerson, subject.ToString(), AsTimestamptz(since));

    std::vector<AccessRecord> found;
    found.reserve(result.Size());
    for (const auto& row : result) {
        const auto actor = core::PersonId::Parse(row["actor_id"].As<std::string>());
        const auto kind = ParseResourceKind(row["resource_kind"].As<std::string>());
        const auto outcome = ParseAccessOutcome(row["outcome"].As<std::string>());
        if (!actor.has_value() || !kind.has_value() || !outcome.has_value()) {
            throw std::runtime_error{"identity_access_log: строка не разбирается"};
        }

        auto record = AccessRecord::Of(
            tenant, *actor, subject, *kind, *outcome, AsInstant(row["at"].As<Timestamptz>()));
        if (!record) {
            throw std::runtime_error{"identity_access_log: строка журнала не собирается: " +
                                     record.Failure().Detail()};
        }
        found.push_back(record.Value());
    }
    return found;
}

}  // namespace pdr::identity
