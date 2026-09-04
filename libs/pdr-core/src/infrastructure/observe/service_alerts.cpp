#include "infrastructure/observe/service_alerts.hpp"

#include <cstddef>
#include <string>

#include <userver/alerts/source.hpp>

namespace pdr::infrastructure::observe {
namespace {

/// Источники живут статически намеренно: каждый регистрирует `MetricTag`, а
/// `MetricsStorage` собирает зарегистрированные теги при своём создании — то
/// есть при старте компонентов. Источник, созданный позже, в хранилище не
/// попадает и не поднимается никогда.
const std::array<userver::alerts::Source, kEveryServiceAlert.size()> kSources{
    userver::alerts::Source{"pdr_migrations_not_applied"},
    userver::alerts::Source{"pdr_storage_unreachable"},
    userver::alerts::Source{"pdr_outgoing_calls_refused"},
    userver::alerts::Source{"pdr_job_has_fallen_silent"},
};

const userver::alerts::Source& SourceOf(ServiceAlert alert) noexcept {
    return kSources.at(static_cast<std::size_t>(alert));
}

}  // namespace

std::string_view Name(ServiceAlert alert) noexcept {
    switch (alert) {
        case ServiceAlert::kMigrationsNotApplied:
            return "migrations_not_applied";
        case ServiceAlert::kStorageUnreachable:
            return "storage_unreachable";
        case ServiceAlert::kOutgoingCallsRefused:
            return "outgoing_calls_refused";
        case ServiceAlert::kJobHasFallenSilent:
            return "job_has_fallen_silent";
        case ServiceAlert::kBoundary:
            break;
    }
    return "migrations_not_applied";
}

ServiceAlerts::ServiceAlerts(userver::utils::statistics::MetricsStorage& storage) noexcept
    : storage_{storage} {}

void ServiceAlerts::Raise(ServiceAlert alert, std::chrono::seconds duration) const {
    SourceOf(alert).FireAlert(storage_, duration);
}

void ServiceAlerts::Clear(ServiceAlert alert) const {
    SourceOf(alert).StopAlertNow(storage_);
}

}  // namespace pdr::infrastructure::observe
