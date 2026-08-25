#include "jobs/infrastructure/postgres_job_journal.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>

#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/io/chrono.hpp>
#include <userver/storages/postgres/query.hpp>

namespace pdr::jobs {
namespace {

using Timestamptz = userver::storages::postgres::TimePointTz;

Timestamptz AsTimestamptz(core::Instant instant) {
    return Timestamptz{userver::storages::postgres::TimePoint{
        std::chrono::duration_cast<userver::storages::postgres::TimePoint::duration>(
            std::chrono::microseconds{instant.UnixMicros()})}};
}

core::Instant AsInstant(Timestamptz value) {
    return core::Instant::FromUnixMicros(std::chrono::duration_cast<std::chrono::microseconds>(
                                             value.GetUnderlying().time_since_epoch())
                                             .count());
}

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

/// Начало попытки не стирает последний завершённый прогон: пока идёт новый,
/// возраст последнего удачного — единственное, что вообще известно о задании.
const userver::storages::postgres::Query kStarted{
    "INSERT INTO jobs_run (job, attempt_at, outcome, runs) "
    "VALUES ($1, $2, 'running', 1) "
    "ON CONFLICT (job) DO UPDATE "
    "SET attempt_at = excluded.attempt_at, outcome = 'running', runs = jobs_run.runs + 1",
    userver::storages::postgres::Query::Name{"jobs_run_started"},
};

const userver::storages::postgres::Query kFinished{
    "UPDATE jobs_run "
    "SET started_at = $2, finished_at = $3, duration_ms = $4, outcome = $5, "
    "produced = $6, repeated = $7 "
    "WHERE job = $1",
    userver::storages::postgres::Query::Name{"jobs_run_finished"},
};

/// Незавершённый прогон записью о прогоне не считается: воркер, упавший
/// посреди работы, обязан выглядеть как замолчавшее задание, а не как
/// отработавшее.
const userver::storages::postgres::Query kLast{
    "SELECT started_at, finished_at, outcome, produced, repeated "
    "FROM jobs_run "
    "WHERE job = $1 AND finished_at IS NOT NULL",
    userver::storages::postgres::Query::Name{"jobs_run_last"},
};

}  // namespace

PostgresJobJournal::PostgresJobJournal(userver::storages::postgres::ClusterPtr cluster)
    : cluster_{std::move(cluster)} {}

void PostgresJobJournal::Started(const JobName& job, core::Instant at) {
    cluster_->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      kStarted,
                      job.Value(),
                      AsTimestamptz(at));
}

void PostgresJobJournal::Finished(const JobName& job, const RunRecord& record) {
    const auto took_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(record.Took()).count();
    cluster_->Execute(userver::storages::postgres::ClusterHostType::kMaster,
                      kFinished,
                      job.Value(),
                      AsTimestamptz(record.StartedAt()),
                      AsTimestamptz(record.FinishedAt()),
                      static_cast<std::int64_t>(took_ms),
                      std::string{Name(record.Result())},
                      record.Produced(),
                      record.Repeated());
}

std::optional<RunRecord> PostgresJobJournal::Last(const JobName& job) const {
    const auto result = cluster_->Execute(
        userver::storages::postgres::ClusterHostType::kMaster, kLast, job.Value());
    if (result.IsEmpty()) {
        return std::nullopt;
    }

    const auto row = result.Front();
    const auto outcome = OutcomeFrom(row["outcome"].As<std::string>());
    if (!outcome.has_value()) {
        return std::nullopt;
    }

    const auto record = RunRecord::Compose(AsInstant(row["started_at"].As<Timestamptz>()),
                                           AsInstant(row["finished_at"].As<Timestamptz>()),
                                           *outcome,
                                           row["produced"].As<std::int64_t>(),
                                           row["repeated"].As<std::int64_t>());
    if (!record.HasValue()) {
        return std::nullopt;
    }
    return record.Value();
}

}  // namespace pdr::jobs
