#include "scheduling/infrastructure/dynamic_config_booking_windows.hpp"

#include <chrono>
#include <optional>

#include <dynamic_config/variables/PDR_BOOKING_WINDOWS.hpp>
#include <dynamic_config/variables/PDR_SCHEDULE_HORIZON.hpp>

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

::dynamic_config::pdr_booking_windows::VariableType Written(std::optional<int> book,
                                                            std::optional<int> reschedule,
                                                            std::optional<int> cancel) {
    ::dynamic_config::pdr_booking_windows::VariableType value;
    value.booking_before_hours = book;
    value.reschedule_before_hours = reschedule;
    value.cancel_before_hours = cancel;
    value.free_cancel_before_hours = 24;
    return value;
}

::dynamic_config::pdr_schedule_horizon::VariableType Horizon(int open_days) {
    ::dynamic_config::pdr_schedule_horizon::VariableType value;
    value.open_days = open_days;
    value.review_period_hours = 24;
    return value;
}

}  // namespace

/// ОБЯЗАТЕЛЬНОЕ ТРЕБОВАНИЕ ЗАДАЧИ: всё работает без единой настройки.
///
/// Источник конфигов недоступен — процесс поднимается на умолчаниях из кода, и
/// они годные. Умолчание, которое домен тут же объявляет негодным, — ловушка,
/// не видная ни в одном тесте: расписание отказывало бы всем, а причина была бы
/// написана только в журнале.
UTEST(DynamicConfigBookingWindows, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigBookingWindows windows{storage.GetSource()};

    const auto said = windows.ForPractice(Tenant());

    ASSERT_TRUE(said.HasValue()) << said.Failure().Code();
    EXPECT_EQ(said.Value().MinNoticeBook(), 2h);
    EXPECT_EQ(said.Value().MaxHorizonBook(), 30 * 24h);
    EXPECT_EQ(said.Value().MinNoticeReschedule(), 2h);
    EXPECT_EQ(said.Value().MinNoticeCancel(), std::nullopt)
        << "окно отмены завелось само: отменить нельзя было бы там, где достаточно взять деньги";
}

/// ПРОПУЩЕННАЯ ВЕЛИЧИНА — «БЕЗ ОГРАНИЧЕНИЯ», А НЕ НОЛЬ. Разбирает это штатный
/// chaotic: необязательное поле схемы приходит пустым, и пустота едет в домен
/// как пустота, а не как ноль.
UTEST(DynamicConfigBookingWindows, AMissingValueMeansNoLimitAtAll) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_BOOKING_WINDOWS, Written(std::nullopt, std::nullopt, 6)}});
    const DynamicConfigBookingWindows windows{storage.GetSource()};

    const auto said = windows.ForPractice(Tenant());

    ASSERT_TRUE(said.HasValue());
    EXPECT_EQ(said.Value().MinNoticeBook(), std::nullopt);
    EXPECT_EQ(said.Value().MinNoticeReschedule(), std::nullopt);
    EXPECT_EQ(said.Value().MinNoticeCancel(), 6h);
}

UTEST(DynamicConfigBookingWindows, AppliesChangeWithoutBeingRecreated) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_BOOKING_WINDOWS, Written(2, 2, std::nullopt)},
         {::dynamic_config::PDR_SCHEDULE_HORIZON, Horizon(30)}});
    const DynamicConfigBookingWindows windows{storage.GetSource()};

    EXPECT_EQ(windows.ForPractice(Tenant()).Value().MinNoticeBook(), 2h);

    storage.Extend({{::dynamic_config::PDR_BOOKING_WINDOWS, Written(48, 12, 1)},
                    {::dynamic_config::PDR_SCHEDULE_HORIZON, Horizon(90)}});

    const auto said = windows.ForPractice(Tenant());
    ASSERT_TRUE(said.HasValue());
    EXPECT_EQ(said.Value().MinNoticeBook(), 48h) << "окно поменяли, а адаптер отдаёт прежнее";
    EXPECT_EQ(said.Value().MaxHorizonBook(), 90 * 24h);
    EXPECT_EQ(said.Value().MinNoticeReschedule(), 12h);
    EXPECT_EQ(said.Value().MinNoticeCancel(), 1h);
}

/// ГОРИЗОНТ БЕРЁТСЯ ИЗ СОСЕДНЕЙ ЗАПИСИ, и это не случайность: «на сколько дней
/// вперёд открыто расписание» уже записано там. Второе место для того же числа
/// разошлось бы с первым.
UTEST(DynamicConfigBookingWindows, TheHorizonComesFromTheScheduleHorizonAlone) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_SCHEDULE_HORIZON, Horizon(7)}});
    const DynamicConfigBookingWindows windows{storage.GetSource()};

    EXPECT_EQ(windows.ForPractice(Tenant()).Value().MaxHorizonBook(), 7 * 24h);
}

}  // namespace pdr::scheduling
