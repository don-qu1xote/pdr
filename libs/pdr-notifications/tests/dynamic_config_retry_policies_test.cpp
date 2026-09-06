#include "notifications/infrastructure/dynamic_config_retry_policies.hpp"

#include <chrono>

#include <dynamic_config/variables/PDR_OUTBOX.hpp>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/utest/utest.hpp>

namespace pdr::notifications {
namespace {

using namespace std::chrono_literals;

::dynamic_config::pdr_outbox::VariableType Written(
    int max_attempts, int first_delay_ms, int attempt_ms, int batch, int poll_ms) {
    ::dynamic_config::pdr_outbox::VariableType value;
    value.max_attempts = max_attempts;
    value.first_delay_ms = first_delay_ms;
    value.attempt_ms = attempt_ms;
    value.batch = batch;
    value.poll_ms = poll_ms;
    return value;
}

}  // namespace

/// Источник конфигов недоступен — процесс поднимается на умолчаниях из кода, и
/// они годные. Умолчание, которое домен тут же объявляет негодным, — ловушка,
/// не видная ни в одном тесте: очередь не разберётся ни разу, а причина будет
/// написана только в журнале.
UTEST(DynamicConfigRetryPolicies, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigRetryPolicies policies{storage.GetSource()};

    const auto policy = policies.Current();

    ASSERT_TRUE(policy.HasValue()) << policy.Failure().Code();
    EXPECT_EQ(policy.Value().MaxAttempts(), 6);
    EXPECT_EQ(policies.Batch(), 32);
    EXPECT_EQ(policies.Poll(), std::chrono::duration_cast<core::Instant::Duration>(5s));
}

UTEST(DynamicConfigRetryPolicies, AppliesChangeWithoutBeingRecreated) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_OUTBOX, Written(6, 60000, 30000, 32, 5000)}});
    const DynamicConfigRetryPolicies policies{storage.GetSource()};

    EXPECT_EQ(policies.Current().Value().MaxAttempts(), 6);

    storage.Extend({{::dynamic_config::PDR_OUTBOX, Written(3, 5000, 10000, 8, 1000)}});

    const auto policy = policies.Current();
    ASSERT_TRUE(policy.HasValue());
    EXPECT_EQ(policy.Value().MaxAttempts(), 3)
        << "предел попыток поменяли в конфиге, а адаптер отдаёт прежний";
    EXPECT_EQ(policies.Batch(), 8);
    EXPECT_EQ(policies.Poll(), std::chrono::duration_cast<core::Instant::Duration>(1s));
}

/// ПРЕДЕЛ ЖИВЁТ В ДОМЕНЕ, А НЕ ТОЛЬКО В СХЕМЕ РЕЕСТРА. Схема отсечёт ноль
/// попыток у того, кто правит значение по правилам; значение, пришедшее мимо
/// схемы, отвергает `RetryPolicy::Compose` — и отвергает целиком.
UTEST(DynamicConfigRetryPolicies, ASettingWithoutASingleAttemptIsRefused) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_OUTBOX, Written(0, 60000, 30000, 32, 5000)}});
    const DynamicConfigRetryPolicies policies{storage.GetSource()};

    const auto policy = policies.Current();

    ASSERT_FALSE(policy.HasValue()) << "предел в ноль попыток доехал до отправщика";
    EXPECT_EQ(policy.Failure().Code(), "retry_attempts_not_positive");
}

}  // namespace pdr::notifications
