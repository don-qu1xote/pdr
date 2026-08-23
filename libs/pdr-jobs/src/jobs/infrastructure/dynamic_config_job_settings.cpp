#include "jobs/infrastructure/dynamic_config_job_settings.hpp"

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <userver/formats/parse/common_containers.hpp>

namespace pdr::jobs {
namespace {

using Jobs = std::unordered_map<std::string, JobSettings>;

const userver::dynamic_config::Key<Jobs> kPeriodicJobs{
    DynamicConfigJobSettings::kVariable,
    userver::dynamic_config::DefaultAsJsonString{"{}"},
};

JobSettings::Duration Milliseconds(const userver::formats::json::Value& value,
                                   std::string_view field) {
    return std::chrono::duration_cast<JobSettings::Duration>(
        std::chrono::milliseconds{value[field].As<std::int64_t>()});
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

DynamicConfigJobSettings::DynamicConfigJobSettings(userver::dynamic_config::Source source) noexcept
    : source_{source} {}

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
