#include "infrastructure/http/dynamic_config_key_lifetime.hpp"

#include <string>

#include <dynamic_config/variables/PDR_IDEMPOTENCY.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::infrastructure::http {

DynamicConfigKeyLifetime::DynamicConfigKeyLifetime(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "http-idempotency-lifetime", &DynamicConfigKeyLifetime::OnConfigUpdate)} {}

DynamicConfigKeyLifetime::~DynamicConfigKeyLifetime() {
    journal_.Unsubscribe();
}

void DynamicConfigKeyLifetime::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto name = std::string{::dynamic_config::PDR_IDEMPOTENCY.GetName()};
    const auto current =
        std::to_string(diff.current[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours) + " часов";
    if (!diff.previous.has_value()) {
        LOG_INFO() << "ключ повтора: первое применение"
                   << userver::logging::LogExtra{
                          {{observe::kConfigKeyField, name}, {observe::kConfigNowField, current}}};
        return;
    }

    const auto previous =
        std::to_string((*diff.previous)[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours) +
        " часов";
    if (previous != current) {
        LOG_INFO() << "ключ повтора: было, стало"
                   << userver::logging::LogExtra{{{observe::kConfigKeyField, name},
                                                  {observe::kConfigWasField, previous},
                                                  {observe::kConfigNowField, current}}};
    }
}

core::Result<pdr::http::KeyLifetime> DynamicConfigKeyLifetime::Lifetime() const {
    const auto snapshot = source_.GetSnapshot();
    return pdr::http::KeyLifetime::Compose(
        snapshot[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours);
}

}  // namespace pdr::infrastructure::http
