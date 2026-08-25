#include "jobs/infrastructure/dynamic_config_job_settings.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <userver/formats/parse/common_containers.hpp>
#include <userver/logging/log.hpp>

namespace pdr::jobs {

const userver::dynamic_config::Key<PeriodicJobs> kPeriodicJobs{
    DynamicConfigJobSettings::kVariable,
    userver::dynamic_config::DefaultAsJsonString{"{}"},
};

namespace {

JobSettings::Duration Milliseconds(const userver::formats::json::Value& value,
                                   std::string_view field) {
    return std::chrono::duration_cast<JobSettings::Duration>(
        std::chrono::milliseconds{value[field].As<std::int64_t>()});
}

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

}  // namespace

JobSettings Parse(const userver::formats::json::Value& value,
                  userver::formats::parse::To<JobSettings>) {
    const auto lock = JobName::Parse(value["lock"].As<std::string>());
    if (!lock.has_value()) {
        throw std::runtime_error{"jobs: имя блокировки не по правилу: " +
                                 value["lock"].As<std::string>()};
    }

    auto settings = JobSettings::Compose(*lock,
                                         Milliseconds(value, "period_ms"),
                                         Milliseconds(value, "attempt_ms"),
                                         Milliseconds(value, "silence_allowed_ms"),
                                         value["enabled"].As<bool>());
    if (!settings.HasValue()) {
        throw std::runtime_error{"jobs: настройки задания не сходятся: " +
                                 settings.Failure().Code() + " — " + settings.Failure().Detail()};
    }
    return settings.Value();
}

DynamicConfigJobSettings::DynamicConfigJobSettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{
          source_.UpdateAndListen(this, kVariable, &DynamicConfigJobSettings::OnConfigUpdate)} {}

DynamicConfigJobSettings::~DynamicConfigJobSettings() {
    journal_.Unsubscribe();
}

void DynamicConfigJobSettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto& current = diff.current[kPeriodicJobs];

    if (!diff.previous.has_value()) {
        LOG_INFO() << std::string{kVariable} << ": первое применение, заданий " << current.size();
        return;
    }

    const auto& previous = (*diff.previous)[kPeriodicJobs];

    for (const auto& [job, settings] : current) {
        const auto was = previous.find(job);
        if (was == previous.end()) {
            LOG_INFO() << std::string{kVariable} << ": задание " << job << " заведено — "
                       << Describe(settings);
        } else if (Describe(was->second) != Describe(settings)) {
            LOG_INFO() << std::string{kVariable} << ": задание " << job << " было ["
                       << Describe(was->second) << "], стало [" << Describe(settings) << "]";
        }
    }

    for (const auto& [job, settings] : previous) {
        if (current.find(job) == current.end()) {
            LOG_INFO() << std::string{kVariable} << ": задание " << job << " убрано — было ["
                       << Describe(settings) << "]";
        }
    }
}

core::Result<JobSettings> DynamicConfigJobSettings::For(const JobName& job) const {
    const auto snapshot = source_.GetSnapshot();
    const auto& jobs = snapshot[kPeriodicJobs];
    const auto found = jobs.find(job.Value());
    if (found == jobs.end()) {
        return core::Error{core::ErrorKind::kNotFound,
                           "job_settings_missing",
                           "задания «" + job.Value() + "» нет в " + std::string{kVariable}};
    }
    return found->second;
}

}  // namespace pdr::jobs
