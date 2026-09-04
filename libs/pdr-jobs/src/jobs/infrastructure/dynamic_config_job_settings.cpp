#include "jobs/infrastructure/dynamic_config_job_settings.hpp"

#include <chrono>
#include <optional>
#include <string>

#include <dynamic_config/variables/PDR_PERIODIC_JOBS.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::jobs {
namespace {

namespace fields = ::pdr::infrastructure::observe;

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

/// `LogExtra::Extend` возвращает void, поэтому поля собираются здесь, а не
/// дописываются на месте вызова цепочкой.
userver::logging::LogExtra About(const std::string& job,
                                 std::optional<std::string> was,
                                 std::optional<std::string> now) {
    userver::logging::LogExtra extra{{fields::kConfigKeyField, Named()},
                                     {fields::kConfigEntryField, job},
                                     {fields::kJobNameField, job}};
    if (was.has_value()) {
        extra.Extend(fields::kConfigWasField, std::move(*was));
    }
    if (now.has_value()) {
        extra.Extend(fields::kConfigNowField, std::move(*now));
    }
    return extra;
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
        LOG_INFO() << "первое применение величины"
                   << userver::logging::LogExtra{
                          {fields::kConfigKeyField, Named()},
                          {fields::kConfigEntriesField, static_cast<int>(current.size())}};
        return;
    }

    const auto& previous = (*diff.previous)[::dynamic_config::PDR_PERIODIC_JOBS].extra;

    for (const auto& [job, settings] : current) {
        const auto was = previous.find(job);
        if (was == previous.end()) {
            LOG_INFO() << "задание заведено" << About(job, std::nullopt, Describe(settings));
        } else if (!(was->second == settings)) {
            LOG_INFO() << "задание изменилось"
                       << About(job, Describe(was->second), Describe(settings));
        }
    }

    for (const auto& [job, settings] : previous) {
        if (current.find(job) == current.end()) {
            LOG_INFO() << "задание убрано" << About(job, Describe(settings), std::nullopt);
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
