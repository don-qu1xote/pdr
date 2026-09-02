#include "identity/infrastructure/access/dynamic_config_maturity_settings.hpp"

#include <dynamic_config/variables/PDR_GUARDIAN_HANDOVER_DAYS.hpp>
#include <dynamic_config/variables/PDR_MAJORITY_AGE.hpp>
#include <dynamic_config/variables/PDR_OWN_PAYMENTS_AGE.hpp>
#include <dynamic_config/variables/PDR_SELF_ACCOUNT_AGE.hpp>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/utest/utest.hpp>

namespace pdr::identity {

/// Источник конфигов недоступен — сервис поднимается на умолчаниях из кода, и
/// они годные. Умолчание, которое домен тут же объявляет негодным, — ловушка,
/// не видная ни в одном тесте.
UTEST(DynamicConfigMaturitySettings, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigMaturitySettings settings{storage.GetSource()};

    const auto rule = settings.Rule();
    ASSERT_TRUE(rule.HasValue()) << rule.Failure().Code();

    const auto& thresholds = rule.Value().Thresholds();
    EXPECT_EQ(thresholds.Years(AgeThreshold::kSlotsAndReviews), 14);
    EXPECT_EQ(thresholds.Years(AgeThreshold::kOwnPayments), 16);
    EXPECT_EQ(thresholds.Years(AgeThreshold::kMajority), 18);
}

UTEST(DynamicConfigMaturitySettings, AppliesChangeWithoutBeingRecreated) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{::dynamic_config::PDR_OWN_PAYMENTS_AGE, 16}});
    const DynamicConfigMaturitySettings settings{storage.GetSource()};

    EXPECT_EQ(settings.Rule().Value().Thresholds().Years(AgeThreshold::kOwnPayments), 16);

    storage.Extend({{::dynamic_config::PDR_OWN_PAYMENTS_AGE, 17}});

    EXPECT_EQ(settings.Rule().Value().Thresholds().Years(AgeThreshold::kOwnPayments), 17)
        << "порог поменяли в конфиге, а адаптер отдаёт прежний";
}

/// СВЯЗЬ МЕЖДУ ТРЕМЯ ЧИСЛАМИ СХЕМА РЕЕСТРА НЕ ВЫРАЖАЕТ: каждое по отдельности в
/// пределах, а вместе — набор, в котором право приходит раньше предыдущего.
/// Отвергает такое домен, и отвергает целиком.
UTEST(DynamicConfigMaturitySettings, ThresholdsOutOfOrderAreRefusedWhole) {
    auto storage =
        userver::dynamic_config::MakeDefaultStorage({{::dynamic_config::PDR_SELF_ACCOUNT_AGE, 17},
                                                     {::dynamic_config::PDR_OWN_PAYMENTS_AGE, 16},
                                                     {::dynamic_config::PDR_MAJORITY_AGE, 18}});
    const DynamicConfigMaturitySettings settings{storage.GetSource()};

    const auto refused = settings.Rule();

    ASSERT_FALSE(refused.HasValue()) << "порядок порогов никто не проверил";
    EXPECT_EQ(refused.Failure().Code(), "age_thresholds_out_of_order");
}

/// Нулевое окно — мгновенный обрыв доступа в день рождения. Тоже отказ, а не
/// «ноль так ноль».
UTEST(DynamicConfigMaturitySettings, ZeroHandoverWindowIsRefused) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_GUARDIAN_HANDOVER_DAYS, 0}});
    const DynamicConfigMaturitySettings settings{storage.GetSource()};

    const auto refused = settings.Rule();

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "maturity_grace_not_positive");
}

}  // namespace pdr::identity
