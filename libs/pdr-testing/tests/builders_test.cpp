#include <chrono>
#include <stdexcept>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "builders/money_builder.hpp"
#include "core/types/ids.hpp"

namespace pdr::testing {
namespace {

using namespace std::chrono_literals;

TEST(NumberedIdentifier, IsReadableAndStable) {
    EXPECT_EQ(Numbered<core::TenantId>(1).ToString(), "00000000-0000-0000-0000-000000000001");
    EXPECT_EQ(Numbered<core::PersonId>(255).ToString(), "00000000-0000-0000-0000-0000000000ff");
    EXPECT_EQ(Numbered<core::LessonId>(4097).ToString(), "00000000-0000-0000-0000-000000001001");

    EXPECT_TRUE(Numbered<core::TenantId>(7) == Numbered<core::TenantId>(7));
    EXPECT_FALSE(Numbered<core::TenantId>(7) == Numbered<core::TenantId>(8));
}

TEST(MoneyBuilder, BuildsRublesAsMinorUnits) {
    const auto price = MoneyBuilder{}.Rubles(1500).Build();

    EXPECT_EQ(price.MinorUnits(), 150000);
    EXPECT_EQ(price.Currency().View(), "RUB");
}

TEST(MoneyBuilder, BuildsMinorUnitsAndOtherCurrencies) {
    const auto kopecks = MoneyBuilder{}.Minor(1).Build();
    EXPECT_EQ(kopecks.MinorUnits(), 1);

    const auto foreign = MoneyBuilder{}.Minor(999).Currency("KZT").Build();
    EXPECT_EQ(foreign.Currency().View(), "KZT");
}

TEST(MoneyBuilder, RefusesCurrencyThatIsNotACode) {
    EXPECT_THROW(MoneyBuilder{}.Minor(1).Currency("рубль").Build(), std::logic_error);
}

TEST(MomentBuilder, BuildsMomentInUtc) {
    // 2024-01-01T00:00:00Z — та же точка отсчёта, что у фейковых часов.
    const auto beginning = MomentBuilder{}.Utc(2024, 1, 1).At(0, 0).Build();
    EXPECT_EQ(beginning.UnixMicros(), 1704067200000000);

    const auto lesson = MomentBuilder{}.Utc(2026, 3, 1).At(18, 30).Build();
    const auto same = MomentBuilder{}.Utc(2026, 3, 1).At(17, 30).Plus(1h).Build();
    EXPECT_TRUE(lesson == same);

    EXPECT_TRUE(MomentBuilder{}.Utc(2026, 3, 1).At(18, 30).Minus(24h).Build() ==
                MomentBuilder{}.Utc(2026, 2, 28).At(18, 30).Build());
}

TEST(MomentBuilder, RefusesWhatIsNotADate) {
    EXPECT_THROW(MomentBuilder{}.Utc(2026, 2, 30), std::logic_error);
    EXPECT_THROW(MomentBuilder{}.At(24, 0), std::logic_error);
    EXPECT_THROW(MomentBuilder{}.At(12, 60), std::logic_error);
}

}  // namespace
}  // namespace pdr::testing
