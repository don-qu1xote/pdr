#include "jobs/infrastructure/dynamic_config_job_settings.hpp"

#include <chrono>
#include <string>

#include <dynamic_config/variables/PDR_PERIODIC_JOBS.hpp>

#include <userver/logging/log.hpp>

namespace pdr::jobs {
namespace {

std::int64_t Ms(JobSettings::Duration duration) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

std::string Describe(const JobSettings& settings) {
    return "lock=" + settings.Lock().Value() +
           " period_ms=" + std::to_string(Ms(settings.Period())) +
           " attempt_ms=" + std::to_string(Ms(settings.Attempt())) +
           " silence_allowed_ms=" + std::to_string(Ms(settings.SilenceAllowed())) +
           " enabled=" + (settings.Enabled() ? "true" : "false");
}

std::string Named() {
    return std::string{::dynamic_config::PDR_PERIODIC_JOBS.GetName()};
}

}  // namespace

DynamicConfigJobSettings::DynamicConfigJobSettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "jobs-periodic-settings", &DynamicConfigJobSettings::OnConfigUpdate)} {}

DynamicConfigJobSettings::~DynamicConfigJobSettings() {
    journal_.Unsubscribe();
}

void DynamicConfigJobSettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto& current = diff.current[::dynamic_config::PDR_PERIODIC_JOBS].extra;

    if (!diff.previous.has_value()) {
        LOG_INFO() << Named() << ": первое применение, заданий " << current.size();
        return;
    }

    const auto& previous = (*diff.previous)[::dynamic_config::PDR_PERIODIC_JOBS].extra;

    for (const auto& [job, settings] : current) {
        const auto was = previous.find(job);
        if (was == previous.end()) {
            LOG_INFO() << Named() << ": задание " << job << " заведено — " << Describe(settings);
        } else if (!(was->second == settings)) {
            LOG_INFO() << Named() << ": задание " << job << " было [" << Describe(was->second)
                       << "], стало [" << Describe(settings) << "]";
        }
    }

    for (const auto& [job, settings] : previous) {
        if (current.find(job) == current.end()) {
            LOG_INFO() << Named() << ": задание " << job << " убрано — было [" << Describe(settings)
                       << "]";
        }
    }
}

core::Result<JobSettings> DynamicConfigJobSettings::For(const JobName& job) const {
    const auto snapshot = source_.GetSnapshot();
    const auto& jobs = snapshot[::dynamic_config::PDR_PERIODIC_JOBS].extra;
    const auto found = jobs.find(job.Value());
    if (found == jobs.end()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "job_settings_missing",
                           "задания «" + job.Value() + "» нет в " + Named()};
    }
    return found->second;
}

}  // namespace pdr::jobs
