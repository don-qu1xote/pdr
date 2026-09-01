#include "observability/infrastructure/dynamic_config_stream_settings.hpp"

#include <string>

#include <userver/logging/log.hpp>

namespace pdr::observability {

const userver::dynamic_config::Key<ProductEventsConfig> kProductEvents{
    DynamicConfigStreamSettings::kProductEventsVariable,
    userver::dynamic_config::DefaultAsJsonString{R"({"enabled": true, "retention_days": 730})"},
};

ProductEventsConfig Parse(const userver::formats::json::Value& value,
                          userver::formats::parse::To<ProductEventsConfig>) {
    return ProductEventsConfig{value["enabled"].As<bool>(),
                               value["retention_days"].As<std::int32_t>()};
}

namespace {

std::string Describe(const ProductEventsConfig& settings) {
    return std::string{"enabled="} + (settings.enabled ? "true" : "false") +
           " retention_days=" + std::to_string(settings.retention_days);
}

}  // namespace

DynamicConfigStreamSettings::DynamicConfigStreamSettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "observability-product-events", &DynamicConfigStreamSettings::OnConfigUpdate)} {}

DynamicConfigStreamSettings::~DynamicConfigStreamSettings() {
    journal_.Unsubscribe();
}

void DynamicConfigStreamSettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current[kProductEvents]);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "продуктовый поток: первое применение — " << current;
        return;
    }

    const auto previous = Describe((*diff.previous)[kProductEvents]);
    if (previous != current) {
        LOG_INFO() << "продуктовый поток: было [" << previous << "], стало [" << current << "]";
    }
}

bool DynamicConfigStreamSettings::Enabled() const {
    const auto snapshot = source_.GetSnapshot();
    return snapshot[kProductEvents].enabled;
}

}  // namespace pdr::observability
