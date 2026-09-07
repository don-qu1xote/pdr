#include "scheduling/infrastructure/dynamic_config_calendar_wording.hpp"

#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_CALENDAR_FEED.hpp>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::scheduling {
namespace {

namespace fields = ::pdr::infrastructure::observe;

constexpr std::string_view kKey = "слова ленты календаря";

std::string Describe(const userver::dynamic_config::Snapshot& snapshot) {
    const auto& value = snapshot[::dynamic_config::PDR_CALENDAR_FEED];
    return "name=" + value.name + " lesson=" + value.lesson +
           " lesson_with_name=" + value.lesson_with_name;
}

}  // namespace

DynamicConfigCalendarWording::DynamicConfigCalendarWording(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "scheduling-calendar-wording", &DynamicConfigCalendarWording::OnConfigUpdate)} {}

DynamicConfigCalendarWording::~DynamicConfigCalendarWording() {
    journal_.Unsubscribe();
}

CalendarWording DynamicConfigCalendarWording::Words() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_CALENDAR_FEED];
    return CalendarWording{value.name, value.lesson, value.lesson_with_name};
}

void DynamicConfigCalendarWording::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto current = Describe(diff.current);
    if (!diff.previous.has_value()) {
        LOG_INFO() << "первое применение слов ленты календаря"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigNowField, current}}};
        return;
    }

    const auto previous = Describe(*diff.previous);
    if (previous != current) {
        LOG_INFO() << "слова ленты календаря изменились"
                   << userver::logging::LogExtra{{{fields::kConfigKeyField, std::string{kKey}},
                                                  {fields::kConfigWasField, previous},
                                                  {fields::kConfigNowField, current}}};
    }
}

}  // namespace pdr::scheduling
