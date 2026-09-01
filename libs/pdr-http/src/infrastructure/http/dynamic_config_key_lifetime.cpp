#include "infrastructure/http/dynamic_config_key_lifetime.hpp"

#include <string>

#include <userver/logging/log.hpp>

namespace pdr::infrastructure::http {

const userver::dynamic_config::Key<IdempotencyConfig> kIdempotency{
    DynamicConfigKeyLifetime::kIdempotencyVariable,
    userver::dynamic_config::DefaultAsJsonString{R"({"lifetime_hours": 24})"},
};

IdempotencyConfig Parse(const userver::formats::json::Value& value,
                        userver::formats::parse::To<IdempotencyConfig>) {
    return IdempotencyConfig{value["lifetime_hours"].As<std::int32_t>()};
}

DynamicConfigKeyLifetime::DynamicConfigKeyLifetime(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "http-idempotency-lifetime", &DynamicConfigKeyLifetime::OnConfigUpdate)} {}

DynamicConfigKeyLifetime::~DynamicConfigKeyLifetime() {
    journal_.Unsubscribe();
}

void DynamicConfigKeyLifetime::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = diff.current[kIdempotency].lifetime_hours;
    if (!diff.previous.has_value()) {
        LOG_INFO() << "ключ повтора: первое применение — часов " << current;
        return;
    }

    const auto previous = (*diff.previous)[kIdempotency].lifetime_hours;
    if (previous != current) {
        LOG_INFO() << "ключ повтора: было [часов " << previous << "], стало [часов " << current
                   << "]";
    }
}

core::Result<pdr::http::KeyLifetime> DynamicConfigKeyLifetime::Lifetime() const {
    const auto snapshot = source_.GetSnapshot();
    return pdr::http::KeyLifetime::Compose(snapshot[kIdempotency].lifetime_hours);
}

}  // namespace pdr::infrastructure::http
