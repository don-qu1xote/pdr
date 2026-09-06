#include "scheduling/infrastructure/dynamic_config_booking_windows.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_BOOKING_WINDOWS.hpp>
#include <dynamic_config/variables/PDR_SCHEDULE_HORIZON.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::scheduling {
namespace {

namespace fields = ::pdr::infrastructure::observe;

constexpr std::string_view kKey = "окна бронирования";

std::optional<BookingWindows::Notice> Hours(const std::optional<int>& hours) {
    if (!hours.has_value()) {
        return std::nullopt;
    }
    return std::chrono::duration_cast<BookingWindows::Notice>(std::chrono::hours{*hours});
}

std::string Said(const std::optional<int>& hours) {
    return hours.has_value() ? std::to_string(*hours) : std::string{"без ограничения"};
}

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    const auto& windows = snapshot[::dynamic_config::PDR_BOOKING_WINDOWS];
    const auto& horizon = snapshot[::dynamic_config::PDR_SCHEDULE_HORIZON];
    return "booking_before_hours=" + Said(windows.booking_before_hours) +
           " open_days=" + std::to_string(horizon.open_days) +
           " reschedule_before_hours=" + Said(windows.reschedule_before_hours) +
           " cancel_before_hours=" + Said(windows.cancel_before_hours);
}

}  // namespace

DynamicConfigBookingWindows::DynamicConfigBookingWindows(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "scheduling-booking-windows", &DynamicConfigBookingWindows::OnConfigUpdate)} {}

DynamicConfigBookingWindows::~DynamicConfigBookingWindows() {
    journal_.Unsubscribe();
}

core::Result<BookingWindows> DynamicConfigBookingWindows::ForPractice(
    const core::TenantId& tenant) const {
    static_cast<void>(tenant);

    const auto snapshot = source_.GetSnapshot();
    const auto& windows = snapshot[::dynamic_config::PDR_BOOKING_WINDOWS];
    const auto& horizon = snapshot[::dynamic_config::PDR_SCHEDULE_HORIZON];

    return BookingWindows::Compose(Hours(windows.booking_before_hours),
                                   std::chrono::duration_cast<BookingWindows::Notice>(
                                       std::chrono::hours{24} * horizon.open_days),
                                   Hours(windows.reschedule_before_hours),
                                   Hours(windows.cancel_before_hours));
}

void DynamicConfigBookingWindows::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение окон бронирования"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "окна бронирования изменились"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

}  // namespace pdr::scheduling
