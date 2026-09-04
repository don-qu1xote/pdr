#include "identity/infrastructure/access/dynamic_config_maturity_settings.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_GUARDIAN_HANDOVER_DAYS.hpp>
#include <dynamic_config/variables/PDR_MAJORITY_AGE.hpp>
#include <dynamic_config/variables/PDR_OWN_PAYMENTS_AGE.hpp>
#include <dynamic_config/variables/PDR_SELF_ACCOUNT_AGE.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::identity {

namespace {

namespace fields = ::pdr::infrastructure::observe;

constexpr std::string_view kKey = "права опекуна";

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    return "self_account_age=" + std::to_string(snapshot[::dynamic_config::PDR_SELF_ACCOUNT_AGE]) +
           " own_payments_age=" + std::to_string(snapshot[::dynamic_config::PDR_OWN_PAYMENTS_AGE]) +
           " majority_age=" + std::to_string(snapshot[::dynamic_config::PDR_MAJORITY_AGE]) +
           " handover_days=" +
           std::to_string(snapshot[::dynamic_config::PDR_GUARDIAN_HANDOVER_DAYS]);
}

}  // namespace

DynamicConfigMaturitySettings::DynamicConfigMaturitySettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "identity-maturity-settings", &DynamicConfigMaturitySettings::OnConfigUpdate)} {}

DynamicConfigMaturitySettings::~DynamicConfigMaturitySettings() {
    journal_.Unsubscribe();
}

void DynamicConfigMaturitySettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение прав опекуна"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "права опекуна изменились"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

core::Result<MaturityRule> DynamicConfigMaturitySettings::Rule() const {
    const auto snapshot = source_.GetSnapshot();

    const auto thresholds = AgeThresholds::Compose(snapshot[::dynamic_config::PDR_SELF_ACCOUNT_AGE],
                                                   snapshot[::dynamic_config::PDR_OWN_PAYMENTS_AGE],
                                                   snapshot[::dynamic_config::PDR_MAJORITY_AGE]);
    if (!thresholds) {
        return thresholds.Failure();
    }

    const auto days = snapshot[::dynamic_config::PDR_GUARDIAN_HANDOVER_DAYS];
    return MaturityRule::Compose(
        thresholds.Value(),
        std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * days}));
}

}  // namespace pdr::identity
