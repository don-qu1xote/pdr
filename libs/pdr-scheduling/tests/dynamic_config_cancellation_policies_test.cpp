#include "scheduling/infrastructure/dynamic_config_cancellation_policies.hpp"

#include <chrono>

#include <dynamic_config/variables/PDR_BOOKING_WINDOWS.hpp>
#include <dynamic_config/variables/PDR_CANCELLATION_POLICY.hpp>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/utest/utest.hpp>

#include "builders/identifiers.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;

core::TenantId Tenant() {
    return pdr::testing::Numbered<core::TenantId>(1);
}

::dynamic_config::pdr_cancellation_policy::VariableType Written(int late,
                                                                int no_show,
                                                                int free_reschedules) {
    ::dynamic_config::pdr_cancellation_policy::VariableType value;
    value.late_retention_percent = late;
    value.no_show_retention_percent = no_show;
    value.free_reschedules = free_reschedules;
    return value;
}

::dynamic_config::pdr_booking_windows::VariableType Windows(int free_cancel_before_hours) {
    ::dynamic_config::pdr_booking_windows::VariableType value;
    value.booking_before_hours = 2;
    value.free_cancel_before_hours = free_cancel_before_hours;
    return value;
}

}  // namespace

/// Источник конфигов недоступен — процесс поднимается на умолчаниях из кода, и
/// они годные. Умолчание, которое домен тут же объявляет негодным, — ловушка,
/// не видная ни в одном тесте.
UTEST(DynamicConfigCancellationPolicies, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigCancellationPolicies policies{storage.GetSource()};

    const auto policy = policies.Of(Tenant());

    ASSERT_TRUE(policy.HasValue()) << policy.Failure().Code();
    EXPECT_EQ(policy.Value().FreeWindow(),
              std::chrono::duration_cast<CancellationPolicy::Window>(24h));
    EXPECT_EQ(policy.Value().LateRetention().Value(), 50);
    EXPECT_EQ(policy.Value().NoShowRetention().Value(), 100);
    EXPECT_EQ(policy.Value().FreeReschedules(), 1);
}

UTEST(DynamicConfigCancellationPolicies, AppliesChangeWithoutBeingRecreated) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_CANCELLATION_POLICY, Written(50, 100, 1)}});
    const DynamicConfigCancellationPolicies policies{storage.GetSource()};

    EXPECT_EQ(policies.Of(Tenant()).Value().LateRetention().Value(), 50);

    storage.Extend({{::dynamic_config::PDR_CANCELLATION_POLICY, Written(30, 80, 0)},
                    {::dynamic_config::PDR_BOOKING_WINDOWS, Windows(48)}});

    const auto policy = policies.Of(Tenant());
    ASSERT_TRUE(policy.HasValue());
    EXPECT_EQ(policy.Value().LateRetention().Value(), 30)
        << "долю поменяли в конфиге, а адаптер отдаёт прежнюю";
    EXPECT_EQ(policy.Value().FreeWindow(),
              std::chrono::duration_cast<CancellationPolicy::Window>(48h));
    EXPECT_EQ(policy.Value().FreeReschedules(), 0);
}

/// ПРЕДЕЛ ДОЛИ ЖИВЁТ В ДОМЕНЕ, А НЕ ТОЛЬКО В СХЕМЕ РЕЕСТРА. Схема отсечёт сто
/// первый процент у того, кто правит значение по правилам; значение, пришедшее
/// мимо схемы, отвергает `core::Percent`, и отвергает целиком — прежнее
/// продолжает действовать.
UTEST(DynamicConfigCancellationPolicies, AShareOutsideItsLimitsIsRefusedWhole) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_CANCELLATION_POLICY, Written(150, 100, 1)}});
    const DynamicConfigCancellationPolicies policies{storage.GetSource()};

    const auto refused = policies.Of(Tenant());

    ASSERT_FALSE(refused.HasValue()) << "долю в полтораста процентов никто не проверил";
    EXPECT_EQ(refused.Failure().Code(), "cancellation_share_out_of_range");
}

}  // namespace pdr::scheduling
