#include "identity/core/password.hpp"

#include <cstddef>
#include <string>

#include <gtest/gtest.h>

namespace pdr::identity {
namespace {

PasswordRules Rules(std::size_t min_length = 10) {
    return PasswordRules::Compose(19456, 2, 1, min_length).Value();
}

TEST(PasswordRules, CheapCostIsRefused) {
    const auto weak = PasswordRules::Compose(1024, 2, 1, 10);

    ASSERT_FALSE(weak.HasValue());
    EXPECT_EQ(weak.Failure().Code(), "password_memory_too_small");
}

TEST(PasswordRules, OneIterationIsNotEnough) {
    const auto weak = PasswordRules::Compose(19456, 1, 1, 10);

    ASSERT_FALSE(weak.HasValue());
    EXPECT_EQ(weak.Failure().Code(), "password_iterations_too_few");
}

TEST(PasswordRules, ShortThresholdIsRefused) {
    const auto weak = PasswordRules::Compose(19456, 2, 1, 4);

    ASSERT_FALSE(weak.HasValue());
    EXPECT_EQ(weak.Failure().Code(), "password_min_length_too_low");
}

TEST(PasswordRules, ThresholdAboveWhatIsAcceptedIsRefused) {
    const auto absurd = PasswordRules::Compose(19456, 2, 1, Password::kMaxLength + 1);

    ASSERT_FALSE(absurd.HasValue());
    EXPECT_EQ(absurd.Failure().Code(), "password_min_length_above_max");
}

/// ПАРОЛЬ МЕРЯЕТСЯ ЗНАКАМИ, А НЕ БАЙТАМИ. «пароль1234» — десять знаков и
/// восемнадцать байт: считая байты, мы требовали бы от русского пароля вдвое
/// меньше знаков, чем от английского, и никто бы этого не заметил.
TEST(Password, LengthIsCountedInSymbols) {
    const auto rules = Rules(10);

    EXPECT_TRUE(Password::Chosen("пароль1234", rules).HasValue());
    EXPECT_TRUE(Password::Chosen("password12", rules).HasValue());

    const auto short_one = Password::Chosen("пароль", rules);
    ASSERT_FALSE(short_one.HasValue()) << "шесть знаков прошли при пороге в десять";
    EXPECT_EQ(short_one.Failure().Code(), "password_too_short");
}

TEST(Password, GivenPasswordIsNotMeasuredByTheRules) {
    const auto old_and_short = Password::Given("abc");

    EXPECT_TRUE(old_and_short.HasValue())
        << "вход рассказал бы постороннему про наши правила и про то, что запись есть";
}

TEST(Password, AbsurdlyLongOneIsRefusedBothWays) {
    const std::string huge(Password::kMaxLength + 1, 'a');

    EXPECT_FALSE(Password::Given(huge).HasValue());
    EXPECT_FALSE(Password::Chosen(huge, Rules()).HasValue());
}

TEST(Password, SecretIsReachableOnlyByName) {
    const auto password = Password::Given("длинный-пароль");

    ASSERT_TRUE(password.HasValue());
    EXPECT_EQ(password.Value().Secret(), "длинный-пароль");
}

TEST(PasswordHash, OnlyArgon2idRecordIsAccepted) {
    EXPECT_TRUE(PasswordHash::Parse("$argon2id$v=19$m=19456,t=2,p=1$c2FsdA$aGFzaA").HasValue());

    const auto legacy = PasswordHash::Parse("5f4dcc3b5aa765d61d8327deb882cf99");
    ASSERT_FALSE(legacy.HasValue()) << "хеш неизвестной схемы принят как пароль";
    EXPECT_EQ(legacy.Failure().Code(), "password_hash_not_argon2id");

    EXPECT_FALSE(PasswordHash::Parse("$argon2i$v=19$m=19456,t=2,p=1$c2FsdA$aGFzaA").HasValue());
}

}  // namespace
}  // namespace pdr::identity
