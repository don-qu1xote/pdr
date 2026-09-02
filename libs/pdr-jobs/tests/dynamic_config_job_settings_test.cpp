#include "jobs/infrastructure/dynamic_config_job_settings.hpp"

#include <chrono>
#include <string>

#include <dynamic_config/variables/PDR_PERIODIC_JOBS.hpp>

#include <userver/dynamic_config/storage_mock.hpp>
#include <userver/dynamic_config/test_helpers.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/utest/log_capture_fixture.hpp>
#include <userver/utest/utest.hpp>

#include "jobs/core/job_name.hpp"

namespace pdr::jobs {
namespace {

using namespace std::chrono_literals;

JobName Reminders() {
    const auto name = JobName::Parse("notifications.reminders");
    return *name;
}

userver::formats::json::Value Entry(int period_ms, int attempt_ms, int silence_ms) {
    return userver::formats::json::FromString(
        R"({"notifications.reminders": {"lock": "notifications.reminders",)"
        R"( "period_ms": )" +
        std::to_string(period_ms) + R"(, "attempt_ms": )" + std::to_string(attempt_ms) +
        R"(, "silence_allowed_ms": )" + std::to_string(silence_ms) + R"(, "enabled": true}})");
}

using JournalTest = userver::utest::LogCaptureFixture<::testing::Test>;

}  // namespace

UTEST(DynamicConfigJobSettings, WorksOnCodeDefaultsWhenSourceGaveNothing) {
    auto storage = userver::dynamic_config::MakeDefaultStorage({});
    const DynamicConfigJobSettings settings{storage.GetSource()};

    const auto found = settings.For(Reminders());
    ASSERT_FALSE(found.HasValue());
    EXPECT_EQ(found.Failure().Code(), "job_settings_missing");
}

UTEST(DynamicConfigJobSettings, AppliesChangeWithoutBeingRecreated) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_PERIODIC_JOBS, Entry(3600000, 600000, 86400000)}});
    const DynamicConfigJobSettings settings{storage.GetSource()};

    const auto before = settings.For(Reminders());
    ASSERT_TRUE(before.HasValue());
    EXPECT_TRUE(before.Value().Period() == 1h);

    storage.Extend({{::dynamic_config::PDR_PERIODIC_JOBS, Entry(1800000, 600000, 86400000)}});

    const auto after = settings.For(Reminders());
    ASSERT_TRUE(after.HasValue());
    EXPECT_TRUE(after.Value().Period() == 30min);
}

UTEST(DynamicConfigJobSettings, RefusesValueOutsideItsRangeAndKeepsTheOldOne) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_PERIODIC_JOBS, Entry(3600000, 600000, 86400000)}});
    const DynamicConfigJobSettings settings{storage.GetSource()};

    EXPECT_THROW(
        storage.Extend({{::dynamic_config::PDR_PERIODIC_JOBS, Entry(600000, 3600000, 86400000)}}),
        std::exception);

    const auto after = settings.For(Reminders());
    ASSERT_TRUE(after.HasValue());
    EXPECT_TRUE(after.Value().Period() == 1h);
    EXPECT_TRUE(after.Value().Attempt() == 10min);
}

UTEST_F(JournalTest, WritesWhatChangedFromWhatToWhat) {
    auto storage = userver::dynamic_config::MakeDefaultStorage(
        {{::dynamic_config::PDR_PERIODIC_JOBS, Entry(3600000, 600000, 86400000)}});
    const DynamicConfigJobSettings settings{storage.GetSource()};

    EXPECT_FALSE(GetLogCapture().Filter("первое применение").empty());

    storage.Extend({{::dynamic_config::PDR_PERIODIC_JOBS, Entry(1800000, 600000, 86400000)}});

    const auto records = GetLogCapture().Filter("было [");
    ASSERT_FALSE(records.empty());
    const auto& text = records.front().GetText();
    EXPECT_NE(text.find("notifications.reminders"), std::string::npos);
    EXPECT_NE(text.find("period_ms=3600000"), std::string::npos);
    EXPECT_NE(text.find("period_ms=1800000"), std::string::npos);
}

}  // namespace pdr::jobs
