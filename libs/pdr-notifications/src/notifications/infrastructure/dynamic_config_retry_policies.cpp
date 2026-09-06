#include "notifications/infrastructure/dynamic_config_retry_policies.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_OUTBOX.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::notifications {
namespace {

namespace fields = ::pdr::infrastructure::observe;

constexpr std::string_view kKey = "разбор очереди";

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    const auto& value = snapshot[::dynamic_config::PDR_OUTBOX];
    return "max_attempts=" + std::to_string(value.max_attempts) +
           " first_delay_ms=" + std::to_string(value.first_delay_ms) +
           " attempt_ms=" + std::to_string(value.attempt_ms) +
           " batch=" + std::to_string(value.batch) + " poll_ms=" + std::to_string(value.poll_ms);
}

}  // namespace

DynamicConfigRetryPolicies::DynamicConfigRetryPolicies(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "notifications-outbox-settings", &DynamicConfigRetryPolicies::OnConfigUpdate)} {}

DynamicConfigRetryPolicies::~DynamicConfigRetryPolicies() {
    journal_.Unsubscribe();
}

core::Result<RetryPolicy> DynamicConfigRetryPolicies::Current() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_OUTBOX];

    return RetryPolicy::Compose(value.max_attempts,
                                std::chrono::milliseconds{value.first_delay_ms},
                                std::chrono::milliseconds{value.attempt_ms});
}

int DynamicConfigRetryPolicies::Batch() const {
    const auto snapshot = source_.GetSnapshot();
    return snapshot[::dynamic_config::PDR_OUTBOX].batch;
}

core::Instant::Duration DynamicConfigRetryPolicies::Poll() const {
    const auto snapshot = source_.GetSnapshot();
    return std::chrono::milliseconds{snapshot[::dynamic_config::PDR_OUTBOX].poll_ms};
}

void DynamicConfigRetryPolicies::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение настроек очереди"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "настройки очереди изменились"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

}  // namespace pdr::notifications
