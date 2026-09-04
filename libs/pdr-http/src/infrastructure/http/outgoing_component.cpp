#include "infrastructure/http/outgoing_component.hpp"

#include <chrono>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

#include <dynamic_config/variables/PDR_OUTGOING_CALLS.hpp>
#include <dynamic_config/variables/PDR_REQUEST_DEADLINE.hpp>

#include <userver/clients/http/component.hpp>
#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/logging/log.hpp>
#include <userver/utils/statistics/writer.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace pdr::infrastructure::http {
namespace {

using Settings = ::dynamic_config::pdr_outgoing_calls::VariableTypeRaw::Extra;

std::string Named() {
    return std::string{::dynamic_config::PDR_OUTGOING_CALLS.GetName()};
}

userver::utils::RetryBudgetSettings BudgetOf(const Settings& settings) {
    userver::utils::RetryBudgetSettings budget;
    budget.max_tokens = static_cast<float>(settings.retry_max_tokens);
    budget.token_ratio = static_cast<float>(settings.retry_token_ratio);
    budget.enabled = settings.attempts > 1;
    return budget;
}

std::size_t BurstOf(const Settings& settings) {
    return static_cast<std::size_t>(settings.rate_burst > 0 ? settings.rate_burst
                                                            : settings.rate_per_second);
}

userver::utils::TokenBucket::RefillPolicy RefillOf(const Settings& settings) {
    return {static_cast<std::size_t>(settings.rate_per_second), std::chrono::seconds{1}};
}

userver::utils::TokenBucket QuotaOf(const Settings& settings) {
    if (settings.rate_per_second <= 0) {
        return userver::utils::TokenBucket::MakeUnbounded();
    }
    return userver::utils::TokenBucket{BurstOf(settings), RefillOf(settings)};
}

std::unordered_map<std::string, std::chrono::milliseconds> TimeoutsOf(
    const userver::dynamic_config::Snapshot& config) {
    const auto& configured = config[::dynamic_config::PDR_OUTGOING_CALLS].extra;

    std::unordered_map<std::string, std::chrono::milliseconds> timeouts;
    timeouts.reserve(configured.size());
    for (const auto& [name, settings] : configured) {
        timeouts.emplace(name, std::chrono::milliseconds{settings.timeout_ms});
    }
    return timeouts;
}

}  // namespace

std::optional<std::string> WhatIsWrongWithDeadlines(
    const std::unordered_map<std::string, std::chrono::milliseconds>& directions,
    std::chrono::milliseconds deadline) {
    for (const auto& [name, timeout] : directions) {
        if (timeout < deadline) {
            continue;
        }
        return "у направления «" + name + "» срок " + std::to_string(timeout.count()) +
               " мс не меньше срока запроса " + std::to_string(deadline.count()) +
               " мс. Вызов пережил бы собственный запрос и держал бы соединение впустую";
    }
    return std::nullopt;
}

OutgoingCallsComponent::Direction::Direction(std::chrono::milliseconds timeout,
                                             int attempts,
                                             const userver::utils::RetryBudgetSettings& budget,
                                             userver::utils::TokenBucket quota)
    : timeout_ms{timeout.count()}, attempts{attempts}, budget{budget}, quota{std::move(quota)} {}

OutgoingCallsComponent::OutgoingCallsComponent(const userver::components::ComponentConfig& config,
                                               const userver::components::ComponentContext& context)
    : ComponentBase{config, context},
      client_{context.FindComponent<userver::components::HttpClient>().GetHttpClient()} {
    auto source = context.FindComponent<userver::components::DynamicConfig>().GetSource();
    const auto snapshot = source.GetSnapshot();
    const auto deadline =
        std::chrono::milliseconds{snapshot[::dynamic_config::PDR_REQUEST_DEADLINE]};

    if (const auto wrong = WhatIsWrongWithDeadlines(TimeoutsOf(snapshot), deadline)) {
        throw std::runtime_error{"сервис не поднимается: " + *wrong};
    }

    for (const auto& [name, settings] : snapshot[::dynamic_config::PDR_OUTGOING_CALLS].extra) {
        directions_.emplace(
            name,
            std::make_unique<Direction>(std::chrono::milliseconds{settings.timeout_ms},
                                        settings.attempts,
                                        BudgetOf(settings),
                                        QuotaOf(settings)));
    }

    statistics_ =
        context.FindComponent<userver::components::StatisticsStorage>().GetStorage().RegisterWriter(
            "outgoing",
            [this](userver::utils::statistics::Writer& writer) { DumpMetrics(writer); });

    LOG_INFO() << "направления наружу проверены: " << directions_.size() << ", срок запроса "
               << deadline.count() << " мс";

    journal_ = source.UpdateAndListen(
        this, "http-outgoing-calls", &OutgoingCallsComponent::OnConfigUpdate);
}

OutgoingCallsComponent::~OutgoingCallsComponent() {
    journal_.Unsubscribe();
    statistics_.Unregister();
}

void OutgoingCallsComponent::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    const auto deadline =
        std::chrono::milliseconds{diff.current[::dynamic_config::PDR_REQUEST_DEADLINE]};
    if (const auto wrong = WhatIsWrongWithDeadlines(TimeoutsOf(diff.current), deadline)) {
        LOG_ERROR() << Named() << ": не применён целиком, направления работают по прежним числам — "
                    << *wrong;
        return;
    }

    const auto& current = diff.current[::dynamic_config::PDR_OUTGOING_CALLS].extra;
    for (const auto& [name, settings] : current) {
        const auto found = directions_.find(name);
        if (found == directions_.end()) {
            LOG_WARNING() << Named() << ": направление " << name
                          << " заводится на старте — до перезапуска наружу не ходит";
            continue;
        }
        auto& direction = *found->second;
        direction.timeout_ms.store(settings.timeout_ms);
        direction.attempts.store(settings.attempts);
        direction.budget.SetSettings(BudgetOf(settings));
        if (settings.rate_per_second <= 0) {
            direction.quota.SetInstantRefillPolicy();
        } else {
            direction.quota.SetMaxSize(BurstOf(settings));
            direction.quota.SetRefillPolicy(RefillOf(settings));
        }
    }

    if (!diff.previous.has_value()) {
        LOG_INFO() << Named() << ": первое применение, направлений " << directions_.size();
        return;
    }

    const auto& previous = (*diff.previous)[::dynamic_config::PDR_OUTGOING_CALLS].extra;
    for (const auto& [name, settings] : current) {
        const auto was = previous.find(name);
        if (was == previous.end()) {
            LOG_INFO() << Named() << ": направление " << name << " заведено — " << settings;
        } else if (!(was->second == settings)) {
            LOG_INFO() << Named() << ": направление " << name << " было [" << was->second
                       << "], стало [" << settings << "]";
        }
    }

    for (const auto& [name, settings] : previous) {
        if (current.find(name) == current.end()) {
            LOG_WARNING() << Named() << ": направление " << name
                          << " убрано из конфига, но работает по прежним числам до перезапуска — "
                          << settings;
        }
    }
}

Outgoing OutgoingCallsComponent::For(std::string_view direction) const {
    const auto found = directions_.find(std::string{direction});
    if (found == directions_.end()) {
        throw std::runtime_error{"направления «" + std::string{direction} +
                                 "» нет в PDR_OUTGOING_CALLS"};
    }

    auto& settings = *found->second;
    return Outgoing{found->first,
                    client_,
                    std::chrono::milliseconds{settings.timeout_ms.load()},
                    settings.attempts.load(),
                    settings.budget,
                    settings.quota};
}

void OutgoingCallsComponent::DumpMetrics(userver::utils::statistics::Writer& writer) const {
    for (const auto& [name, settings] : directions_) {
        writer["retries"].ValueWithLabels(settings->budget, {{"direction", name}});
        writer["quota"].ValueWithLabels(settings->quota, {{"direction", name}});
    }
}

userver::yaml_config::Schema OutgoingCallsComponent::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(R"(
type: object
description: единственная дверь наружу; сроки и повторы живут в PDR_OUTGOING_CALLS
additionalProperties: false
properties: {}
)");
}

}  // namespace pdr::infrastructure::http
