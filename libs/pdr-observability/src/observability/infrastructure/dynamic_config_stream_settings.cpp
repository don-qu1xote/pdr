#include "observability/infrastructure/dynamic_config_stream_settings.hpp"

#include <string>

#include <dynamic_config/variables/PDR_PRODUCT_EVENTS.hpp>

#include <userver/logging/log.hpp>

namespace pdr::observability {
namespace {

using Settings = ::dynamic_config::pdr_product_events::VariableType;

std::string Describe(const Settings& settings) {
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
    const auto current = Describe(diff.current[::dynamic_config::PDR_PRODUCT_EVENTS]);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "продуктовый поток: первое применение — " << current;
        return;
    }

    const auto previous = Describe((*diff.previous)[::dynamic_config::PDR_PRODUCT_EVENTS]);
    if (previous != current) {
        LOG_INFO() << "продуктовый поток: было [" << previous << "], стало [" << current << "]";
    }
}

bool DynamicConfigStreamSettings::Enabled() const {
    const auto snapshot = source_.GetSnapshot();
    return snapshot[::dynamic_config::PDR_PRODUCT_EVENTS].enabled;
}

}  // namespace pdr::observability
