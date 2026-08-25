#include "jobs/core/job_settings.hpp"

#include <utility>

namespace pdr::jobs {

JobSettings::JobSettings(JobName lock,
                         Duration period,
                         Duration attempt,
                         Duration silence_allowed,
                         bool enabled) noexcept
    : lock_{std::move(lock)},
      period_{period},
      attempt_{attempt},
      silence_allowed_{silence_allowed},
      enabled_{enabled} {}

core::Result<JobSettings> JobSettings::Compose(
    JobName lock, Duration period, Duration attempt, Duration silence_allowed, bool enabled) {
    if (period <= Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_period_not_positive",
                           "период задания должен быть положительным"};
    }
    if (attempt <= Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_attempt_not_positive",
                           "время на прогон должно быть положительным"};
    }
    if (attempt > period) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_attempt_over_period",
                           "прогону отведено больше, чем период: прогоны наедут друг на друга"};
    }
    if (silence_allowed < period) {
        return core::Error{core::ErrorKind::kValidation,
                           "job_silence_under_period",
                           "молчание короче периода: тревога сработает на каждом прогоне"};
    }
    return JobSettings{std::move(lock), period, attempt, silence_allowed, enabled};
}

}  // namespace pdr::jobs
