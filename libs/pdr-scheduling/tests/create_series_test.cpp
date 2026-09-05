#include "scheduling/application/create_series.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_scheduling.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeSeries;
using pdr::testing::FakeIdGenerator;
using pdr::testing::Numbered;

core::TenantId Tenant() {
    return Numbered<core::TenantId>(1);
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

core::PersonId Student() {
    return Numbered<core::PersonId>(20);
}

CreateSeries::Request Asking(std::string rrule) {
    return CreateSeries::Request{Tenant(),
                                 Tutor(),
                                 Student(),
                                 std::move(rrule),
                                 core::Date::Compose(2026, 3, 3).Value(),
                                 core::LocalTime::Compose(18, 0).Value(),
                                 core::TimeZone::Parse("Europe/Moscow").value(),
                                 60min};
}

class CreateSeriesTest : public ::testing::Test {
protected:
    CreateSeries Creating() {
        return CreateSeries{series_, ids_};
    }

    FakeSeries series_;
    FakeIdGenerator ids_;
};

TEST_F(CreateSeriesTest, TheSeriesIsKeptAsARuleAndNotAsLessons) {
    const auto created = Creating().Execute(Asking("FREQ=WEEKLY;INTERVAL=1;BYDAY=TU;COUNT=8"));

    ASSERT_TRUE(created.HasValue());
    EXPECT_EQ(created.Value().Rule().Days().size(), 1U);
    EXPECT_EQ(created.Value().Rule().Interval(), 1);

    const auto found = series_.Find(Tenant(), created.Value().Id());
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->At() == core::LocalTime::Compose(18, 0).Value())
        << "время серии уехало: оно хранится местным, а не в UTC";
}

/// Часть вне поддержанного подмножества — НАЗВАННЫЙ отказ, а не тихий пропуск:
/// иначе человек узнаёт о расписании, которого не просил, на занятии, куда
/// никто не пришёл.
TEST_F(CreateSeriesTest, ARuleOutsideTheSubsetIsRefusedAloud) {
    const auto refused = Creating().Execute(Asking("FREQ=WEEKLY;BYDAY=TU;BYSETPOS=1;COUNT=8"));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "recurrence_rule_unsupported");
}

TEST_F(CreateSeriesTest, ARuleWithoutAnEndingIsRefused) {
    const auto refused = Creating().Execute(Asking("FREQ=WEEKLY;BYDAY=TU"));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "recurrence_no_ending");
}

TEST_F(CreateSeriesTest, NothingIsKeptWhenTheRuleIsRefused) {
    ASSERT_FALSE(Creating().Execute(Asking("FREQ=WEEKLY;BYDAY=TU")).HasValue());

    EXPECT_FALSE(series_.Find(Tenant(), Numbered<core::SeriesId>(1)).has_value());
}

}  // namespace
}  // namespace pdr::scheduling
