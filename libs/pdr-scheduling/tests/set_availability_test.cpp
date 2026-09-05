#include "scheduling/application/set_availability.hpp"

#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_scheduling.hpp"
#include "scheduling/application/get_availability.hpp"

namespace pdr::scheduling {
namespace {

using pdr::scheduling::testing::FakeAvailability;
using pdr::testing::Numbered;

core::TenantId Tenant() {
    return Numbered<core::TenantId>(1);
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

core::LocalTime Clock(unsigned hour) {
    return core::LocalTime::Compose(hour, 0).Value();
}

Availability Tuesdays() {
    const auto rule = AvailabilityRule::Compose(core::Weekday::kTuesday,
                                                Clock(10),
                                                Clock(18),
                                                core::TimeZone::Parse("Europe/Moscow").value());
    EXPECT_TRUE(rule.HasValue());
    return Availability::Compose({rule.Value()}, {}).Value();
}

class AvailabilityScenariosTest : public ::testing::Test {
protected:
    FakeAvailability availability_;
};

/// «НЕ ЗАДАВАЛ» — ОТКАЗ, А НЕ ПУСТОЙ ОТВЕТ: пустой ответ значит «не работаю
/// никогда», и это другое утверждение.
TEST_F(AvailabilityScenariosTest, WhatWasNeverSetIsRefusedAndNotReturnedEmpty) {
    const GetAvailability showing{availability_};

    const auto refused = showing.Execute({Tenant(), Tutor()});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kNotFound);
    EXPECT_EQ(refused.Failure().Code(), "availability_not_set");
}

TEST_F(AvailabilityScenariosTest, WhatWasWrittenComesBack) {
    const SetAvailability writing{availability_};
    ASSERT_TRUE(writing.Execute({Tenant(), Tutor(), Tuesdays()}).HasValue());

    const GetAvailability showing{availability_};
    const auto found = showing.Execute({Tenant(), Tutor()});

    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().Rules().size(), 1U);
    EXPECT_EQ(found.Value().Rules().front().Day(), core::Weekday::kTuesday);
}

/// Пустая доступность — законное значение и НЕ то же самое, что отсутствие:
/// репетитор сказал, что не работает, и сказать это он вправе.
TEST_F(AvailabilityScenariosTest, AnEmptyAvailabilityIsNotTheSameAsNone) {
    const SetAvailability writing{availability_};
    ASSERT_TRUE(
        writing.Execute({Tenant(), Tutor(), Availability::Compose({}, {}).Value()}).HasValue());

    const GetAvailability showing{availability_};
    const auto found = showing.Execute({Tenant(), Tutor()});

    ASSERT_TRUE(found.HasValue());
    EXPECT_TRUE(found.Value().Rules().empty());
}

TEST_F(AvailabilityScenariosTest, WritingAgainReplacesEverything) {
    const SetAvailability writing{availability_};
    ASSERT_TRUE(writing.Execute({Tenant(), Tutor(), Tuesdays()}).HasValue());
    ASSERT_TRUE(
        writing.Execute({Tenant(), Tutor(), Availability::Compose({}, {}).Value()}).HasValue());

    const GetAvailability showing{availability_};
    const auto found = showing.Execute({Tenant(), Tutor()});

    ASSERT_TRUE(found.HasValue());
    EXPECT_TRUE(found.Value().Rules().empty()) << "прежнее правило пережило перезапись";
}

}  // namespace
}  // namespace pdr::scheduling
