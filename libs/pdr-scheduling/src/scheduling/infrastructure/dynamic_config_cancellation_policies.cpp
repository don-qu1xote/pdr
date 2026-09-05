#include "scheduling/infrastructure/dynamic_config_cancellation_policies.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_BOOKING_WINDOWS.hpp>
#include <dynamic_config/variables/PDR_CANCELLATION_POLICY.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::scheduling {
namespace {

namespace fields = ::pdr::infrastructure::observe;

constexpr std::string_view kKey = "политика отмены";

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    const auto& windows = snapshot[::dynamic_config::PDR_BOOKING_WINDOWS];
    const auto& value = snapshot[::dynamic_config::PDR_CANCELLATION_POLICY];
    return "free_cancel_before_hours=" + std::to_string(windows.free_cancel_before_hours) +
           " late_retention_percent=" + std::to_string(value.late_retention_percent) +
           " no_show_retention_percent=" + std::to_string(value.no_show_retention_percent) +
           " free_reschedules=" + std::to_string(value.free_reschedules);
}

core::Error OutOfRange(std::string_view what) {
    return core::Error{core::ErrorKind::kValidation,
                       "cancellation_share_out_of_range",
                       "доля «" + std::string{what} + "» не лежит между нулём и сотней"};
}

}  // namespace

DynamicConfigCancellationPolicies::DynamicConfigCancellationPolicies(
    userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(this,
                                       "scheduling-cancellation-policy",
                                       &DynamicConfigCancellationPolicies::OnConfigUpdate)} {}

DynamicConfigCancellationPolicies::~DynamicConfigCancellationPolicies() {
    journal_.Unsubscribe();
}

core::Result<CancellationPolicy> DynamicConfigCancellationPolicies::Of(
    const core::TenantId& tenant) const {
    static_cast<void>(tenant);

    const auto snapshot = source_.GetSnapshot();
    const auto& windows = snapshot[::dynamic_config::PDR_BOOKING_WINDOWS];
    const auto& value = snapshot[::dynamic_config::PDR_CANCELLATION_POLICY];

    const auto late = core::Percent::Compose(value.late_retention_percent);
    if (!late.has_value()) {
        return OutOfRange("late_retention_percent");
    }
    const auto no_show = core::Percent::Compose(value.no_show_retention_percent);
    if (!no_show.has_value()) {
        return OutOfRange("no_show_retention_percent");
    }

    return CancellationPolicy::Compose(std::chrono::hours{windows.free_cancel_before_hours},
                                       *late,
                                       *no_show,
                                       value.free_reschedules);
}

void DynamicConfigCancellationPolicies::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение политики отмены"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "политика отмены изменилась"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

}  // namespace pdr::scheduling
