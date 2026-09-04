#include "observability/infrastructure/dynamic_config_stream_settings.hpp"

#include <string>

#include <dynamic_config/variables/PDR_PRODUCT_EVENTS.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::observability {
namespace {

namespace fields = ::pdr::infrastructure::observe;

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
    const auto name = std::string{::dynamic_config::PDR_PRODUCT_EVENTS.GetName()};
    const auto current = Describe(diff.current[::dynamic_config::PDR_PRODUCT_EVENTS]);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение продуктового потока"
                   << userver::logging::LogExtra{
                          {{fields::kConfigKeyField, name}, {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe((*diff.previous)[::dynamic_config::PDR_PRODUCT_EVENTS]);
    if (previous != current) {
        LOG_INFO() << "продуктовый поток изменился"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, name},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

bool DynamicConfigStreamSettings::Enabled() const {
    const auto snapshot = source_.GetSnapshot();
    return snapshot[::dynamic_config::PDR_PRODUCT_EVENTS].enabled;
}

}  // namespace pdr::observability
