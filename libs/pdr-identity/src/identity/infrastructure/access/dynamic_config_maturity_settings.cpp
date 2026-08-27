#include "identity/infrastructure/access/dynamic_config_maturity_settings.hpp"

#include <chrono>
#include <string>

#include <userver/logging/log.hpp>

namespace pdr::identity {

const userver::dynamic_config::Key<std::int32_t> kSelfAccountAge{
    DynamicConfigMaturitySettings::kAgeVariable,
    userver::dynamic_config::DefaultAsJsonString{"14"},
};

const userver::dynamic_config::Key<std::int32_t> kOwnPaymentsAge{
    DynamicConfigMaturitySettings::kPaymentsAgeVariable,
    userver::dynamic_config::DefaultAsJsonString{"16"},
};

const userver::dynamic_config::Key<std::int32_t> kMajorityAge{
    DynamicConfigMaturitySettings::kMajorityAgeVariable,
    userver::dynamic_config::DefaultAsJsonString{"18"},
};

const userver::dynamic_config::Key<std::int32_t> kGuardianHandoverDays{
    DynamicConfigMaturitySettings::kHandoverVariable,
    userver::dynamic_config::DefaultAsJsonString{"30"},
};

namespace {

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    return "self_account_age=" + std::to_string(snapshot[kSelfAccountAge]) +
           " own_payments_age=" + std::to_string(snapshot[kOwnPaymentsAge]) +
           " majority_age=" + std::to_string(snapshot[kMajorityAge]) +
           " handover_days=" + std::to_string(snapshot[kGuardianHandoverDays]);
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
        LOG_INFO() << "права опекуна: первое применение — " << current;
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "права опекуна: было [" << previous << "], стало [" << current << "]";
    }
}

core::Result<MaturityRule> DynamicConfigMaturitySettings::Rule() const {
    const auto snapshot = source_.GetSnapshot();

    const auto thresholds = AgeThresholds::Compose(
        snapshot[kSelfAccountAge], snapshot[kOwnPaymentsAge], snapshot[kMajorityAge]);
    if (!thresholds) {
        return thresholds.Failure();
    }

    const auto days = snapshot[kGuardianHandoverDays];
    return MaturityRule::Compose(
        thresholds.Value(),
        std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * days}));
}

}  // namespace pdr::identity
