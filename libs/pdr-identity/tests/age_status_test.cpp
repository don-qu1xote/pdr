#include "identity/core/age_status.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "fakes/fake_clock.hpp"
#include "identity/core/person.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::FakeClock;
using pdr::testing::MomentBuilder;

BirthDate Born(int year, unsigned month, unsigned day) {
    return BirthDate::Of(year, month, day).Value();
}

core::Instant Moment(int year, unsigned month, unsigned day, unsigned hour = 12) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, 0).Build();
}

TEST(BirthDate, CalendarIsCheckedAndNothingElse) {
    EXPECT_TRUE(BirthDate::Of(2011, 3, 4).HasValue());
    EXPECT_TRUE(BirthDate::Of(2000, 2, 29).HasValue()) << "високосный год бывает";

    const auto missing = BirthDate::Of(2011, 2, 30);
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Failure().Code(), "birth_date_invalid");

    EXPECT_FALSE(BirthDate::Of(2011, 13, 1).HasValue());
    EXPECT_FALSE(BirthDate::Of(1811, 3, 4).HasValue()) << "год похож на опечатку";
}

/// ГЛАВНЫЙ СЛУЧАЙ ЭТОГО ФАЙЛА: возраст меняется сам, потому что его негде
/// сохранить. Никто ничего не пересчитывает — просто спрашивают у другого
/// момента, и часы для этого подменяются.
TEST(AgeStatus, BirthdayChangesAgeWithoutAnyoneRecalculatingIt) {
    const auto born = Born(2011, 3, 4);
    FakeClock clock{Moment(2026, 3, 3)};

    const auto before = AgeStatus::At(born, clock.Now());
    ASSERT_TRUE(before.HasValue());
    EXPECT_EQ(before.Value().Years(), 14);

    clock.Advance(24h);

    const auto after = AgeStatus::At(born, clock.Now());
    ASSERT_TRUE(after.HasValue());
    EXPECT_EQ(after.Value().Years(), 15) << "день рождения наступил, а возраст остался прежним";

    EXPECT_EQ(before.Value().Years(), 14) << "прежний ответ относится к прежнему моменту";
}

TEST(AgeStatus, BirthdayItselfCountsAsTheNewAge) {
    const auto born = Born(2008, 6, 15);

    EXPECT_EQ(AgeStatus::At(born, Moment(2026, 6, 14)).Value().Years(), 17);
    EXPECT_EQ(AgeStatus::At(born, Moment(2026, 6, 15, 0)).Value().Years(), 18);
    EXPECT_EQ(AgeStatus::At(born, Moment(2026, 6, 15, 23)).Value().Years(), 18);
}

TEST(AgeStatus, LeapDayBirthdayArrivesOnTheFirstOfMarch) {
    const auto born = Born(2008, 2, 29);

    EXPECT_EQ(AgeStatus::At(born, Moment(2026, 2, 28)).Value().Years(), 17);
    EXPECT_EQ(AgeStatus::At(born, Moment(2026, 3, 1)).Value().Years(), 18);
}

/// Порог приходит параметром. Числа 14, 16 и 18 в домене не зашиты: их читает
/// PDR-IDENT-05 из динамического конфига, и меняются они без выкатки.
TEST(AgeStatus, ThresholdComesFromOutside) {
    const auto status = AgeStatus::At(Born(2011, 3, 4), Moment(2026, 3, 4)).Value();

    ASSERT_EQ(status.Years(), 15);
    EXPECT_TRUE(status.Reached(14));
    EXPECT_TRUE(status.Reached(15));
    EXPECT_FALSE(status.Reached(16));
    EXPECT_FALSE(status.Reached(18));
}

TEST(AgeStatus, AnswerIsTiedToTheMomentItWasAskedAbout) {
    const auto born = Born(2011, 3, 4);
    const auto moment = Moment(2026, 3, 4);

    const auto status = AgeStatus::At(born, moment).Value();

    EXPECT_TRUE(status.Moment() == moment);
    EXPECT_TRUE(status.BornOn() == born);
}

TEST(AgeStatus, MomentBeforeBirthIsARefusalAndNotANegativeAge) {
    const auto refused = AgeStatus::At(Born(2011, 3, 4), Moment(2010, 1, 1));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "age_before_birth");
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kValidation);
}

TEST(AgeStatus, PersonAnswersAboutItselfTheSameWay) {
    const Person student{pdr::testing::Numbered<core::PersonId>(20),
                         Email::Parse("student@example.test").Value(),
                         Born(2011, 3, 4)};

    const auto status = student.AgeAt(Moment(2026, 3, 4));

    ASSERT_TRUE(status.HasValue());
    EXPECT_EQ(status.Value().Years(), 15);
}

}  // namespace
}  // namespace pdr::identity
