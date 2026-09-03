#include "identity/infrastructure/access/postgres_access_journal.hpp"

#include <stdexcept>
#include <string>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::AsTimestamptz;
using infrastructure::db::Filled;
using infrastructure::db::Timestamptz;

}  // namespace

PostgresAccessJournal::PostgresAccessJournal(
    infrastructure::db::ScopedTenantContext& scope) noexcept
    : scope_{scope} {}

std::vector<AccessRecord> PostgresAccessJournal::AboutPerson(const core::TenantId& tenant,
                                                             const core::PersonId& subject,
                                                             core::Instant since) const {
    const auto result =
        scope_.Session().Execute(sql::kIdentityAccessLogAboutPerson, subject, since);

    std::vector<AccessRecord> found;
    found.reserve(result.Size());
    for (const auto& row :
         result.AsSetOf<IdentityAccessLogAboutPersonRow>(userver::storages::postgres::kRowTag)) {
        const auto actor = core::PersonId::Parse(Filled(row.actor_id, "actor_id"));
        const auto kind = ParseResourceKind(Filled(row.resource_kind, "resource_kind"));
        const auto outcome = ParseAccessOutcome(Filled(row.outcome, "outcome"));
        if (!actor.has_value() || !kind.has_value() || !outcome.has_value()) {
            throw std::runtime_error{"identity_access_log: строка не разбирается"};
        }

        auto record = AccessRecord::Of(
            tenant, *actor, subject, *kind, *outcome, AsInstant(Filled(row.at, "at")));
        if (!record) {
            throw std::runtime_error{"identity_access_log: строка журнала не собирается: " +
                                     record.Failure().Detail()};
        }
        found.push_back(record.Value());
    }
    return found;
}

}  // namespace pdr::identity
