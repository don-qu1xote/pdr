#include "jobs/application/run_periodic_job.hpp"

namespace pdr::jobs {

RunPeriodicJob::RunPeriodicJob(ports::JobLedger& ledger,
                               ports::JobJournal& journal,
                               const application::ports::Clock& clock) noexcept
    : ledger_{ledger}, journal_{journal}, clock_{clock} {}

RunRecord RunPeriodicJob::Execute(const JobName& job,
                                  const JobSettings& settings,
                                  PeriodicJob& work,
                                  const ports::JobLock& lock) const {
    const auto started_at = clock_.Now();
    journal_.Started(job, started_at);

    auto outcome = Outcome::kDone;
    std::int64_t produced = 0;
    std::int64_t repeated = 0;

    for (const auto& item : work.Plan(started_at)) {
        if (!lock.IsHeld()) {
            outcome = Outcome::kLockLost;
            break;
        }
        if (clock_.Now() - started_at >= settings.Attempt()) {
            outcome = Outcome::kTimedOut;
            break;
        }
        if (!ledger_.Claim(item.tenant, job, item.key)) {
            ++repeated;
            continue;
        }
        work.Perform(item);
        ++produced;
    }

    const auto record = RunRecord::Compose(started_at, clock_.Now(), outcome, produced, repeated);
    journal_.Finished(job, record.Value());
    return record.Value();
}

}  // namespace pdr::jobs
