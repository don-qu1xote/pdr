#include "jobs/core/run_record.hpp"

namespace pdr::jobs {

std::string_view Name(Outcome outcome) noexcept {
    switch (outcome) {
        case Outcome::kDone:
            return "done";
        case Outcome::kLockLost:
            return "lock_lost";
        case Outcome::kTimedOut:
            return "timed_out";
    }
    return "done";
}

RunRecord::RunRecord(core::Instant started_at,
                     core::Instant finished_at,
                     Outcome outcome,
                     std::int64_t produced,
                     std::int64_t repeated) noexcept
    : started_at_{started_at},
      finished_at_{finished_at},
      outcome_{outcome},
      produced_{produced},
      repeated_{repeated} {}

core::Result<RunRecord> RunRecord::Compose(core::Instant started_at,
                                           core::Instant finished_at,
                                           Outcome outcome,
                                           std::int64_t produced,
                                           std::int64_t repeated) {
    if (finished_at < started_at) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_run_ends_before_it_starts",
                           "прогон закончился раньше, чем начался"};
    }
    if (produced < 0 || repeated < 0) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_run_counter_negative",
                           "счётчик действий не бывает отрицательным"};
    }
    return RunRecord{started_at, finished_at, outcome, produced, repeated};
}

core::Instant::Duration SilenceFor(const RunRecord& last, core::Instant now) noexcept {
    if (now <= last.FinishedAt()) {
        return core::Instant::Duration::zero();
    }
    return now - last.FinishedAt();
}

bool HasFallenSilent(const std::optional<RunRecord>& last,
                     core::Instant now,
                     core::Instant::Duration silence_allowed) noexcept {
    if (!last.has_value()) {
        return true;
    }
    return SilenceFor(*last, now) > silence_allowed;
}

}  // namespace pdr::jobs
