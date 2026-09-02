#include "infrastructure/http/dynamic_config_key_lifetime.hpp"

#include <string>

#include <dynamic_config/variables/PDR_IDEMPOTENCY.hpp>

#include <userver/logging/log.hpp>

namespace pdr::infrastructure::http {

DynamicConfigKeyLifetime::DynamicConfigKeyLifetime(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "http-idempotency-lifetime", &DynamicConfigKeyLifetime::OnConfigUpdate)} {}

DynamicConfigKeyLifetime::~DynamicConfigKeyLifetime() {
    journal_.Unsubscribe();
}

void DynamicConfigKeyLifetime::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = diff.current[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours;
    if (!diff.previous.has_value()) {
        LOG_INFO() << "ключ повтора: первое применение — часов " << current;
        return;
    }

    const auto previous = (*diff.previous)[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours;
    if (previous != current) {
        LOG_INFO() << "ключ повтора: было [часов " << previous << "], стало [часов " << current
                   << "]";
    }
}

core::Result<pdr::http::KeyLifetime> DynamicConfigKeyLifetime::Lifetime() const {
    const auto snapshot = source_.GetSnapshot();
    return pdr::http::KeyLifetime::Compose(
        snapshot[::dynamic_config::PDR_IDEMPOTENCY].lifetime_hours);
}

}  // namespace pdr::infrastructure::http
