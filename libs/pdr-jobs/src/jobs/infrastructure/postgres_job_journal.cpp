#include "jobs/infrastructure/postgres_job_journal.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include <pdr/pg_client.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/cluster_types.hpp>

#include "infrastructure/db/columns.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::jobs {
namespace {

using infrastructure::db::AsInstant;
using infrastructure::db::Filled;

std::optional<Outcome> OutcomeFrom(const std::string& stored) {
    if (stored == "done") {
        return Outcome::kDone;
    }
    if (stored == "lock_lost") {
        return Outcome::kLockLost;
    }
    if (stored == "timed_out") {
        return Outcome::kTimedOut;
    }
    return std::nullopt;
}

}  // namespace

PostgresJobJournal::PostgresJobJournal(const infrastructure::db::UnscopedAccess& access) noexcept
    : access_{access} {}

void PostgresJobJournal::Started(const JobName& job, core::Instant at) {
    access_.Execute(sql::kJobsRunStarted, job.Value(), at);
}

void PostgresJobJournal::Finished(const JobName& job, const RunRecord& record) {
    const auto took_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(record.Took()).count();
    access_.Execute(sql::kJobsRunFinished,
                    job.Value(),
                    record.StartedAt(),
                    record.FinishedAt(),
                    static_cast<std::int64_t>(took_ms),
                    Name(record.Result()),
                    record.Produced(),
                    record.Repeated());
}

std::optional<RunRecord> PostgresJobJournal::Last(const JobName& job) const {
    const auto result = access_.Execute(sql::kJobsRunLast, job.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front().As<JobsRunLastRow>(userver::storages::postgres::kRowTag);
    const auto outcome = OutcomeFrom(Filled(row.outcome, "outcome"));
    if (!outcome.has_value()) {
        return std::nullopt;
    }

    const auto record = RunRecord::Compose(AsInstant(Filled(row.started_at, "started_at")),
                                           AsInstant(Filled(row.finished_at, "finished_at")),
                                           *outcome,
                                           Filled(row.produced, "produced"),
                                           Filled(row.repeated, "repeated"));
    if (!record.HasValue()) {
        return std::nullopt;
    }
    return record.Value();
}

}  // namespace pdr::jobs
