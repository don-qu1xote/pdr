#include "identity/core/one_time_token.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "identity/core/auth_lifetimes.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant::Duration Of(std::chrono::hours hours) {
    return std::chrono::duration_cast<core::Instant::Duration>(hours);
}

Digest SomeSecret(int number = 1) {
    std::string hex(64, '0');
    hex.back() = static_cast<char>('0' + number);
    return Digest::Parse(hex).Value();
}

OneTimeToken Invitation(core::Instant at, std::chrono::hours lifetime = 48h) {
    return OneTimeToken::Invitation(Numbered<TokenId>(1),
                                    Numbered<core::TenantId>(1),
                                    SomeSecret(),
                                    Role::kStudent,
                                    std::nullopt,
                                    at,
                                    Of(lifetime))
        .Value();
}

TEST(Digest, OnlyLowercaseHexOfTheRightLength) {
    EXPECT_TRUE(Digest::Parse(std::string(64, 'a')).HasValue());
    EXPECT_FALSE(Digest::Parse(std::string(63, 'a')).HasValue());
    EXPECT_FALSE(Digest::Parse(std::string(65, 'a')).HasValue());
    EXPECT_FALSE(Digest::Parse(std::string(64, 'A')).HasValue());
    EXPECT_FALSE(Digest::Parse(std::string(64, 'z')).HasValue());
}

TEST(TokenSecret, ShortLinkIsRefused) {
    const auto short_one = TokenSecret::Parse(std::string(TokenSecret::kLeastLength - 1, 'a'));

    ASSERT_FALSE(short_one.HasValue());
    EXPECT_EQ(short_one.Failure().Code(), "token_secret_too_short");
}

TEST(TokenSecret, AlphabetIsBase64Url) {
    EXPECT_TRUE(TokenSecret::Parse(std::string(43, 'a')).HasValue());
    EXPECT_TRUE(TokenSecret::Parse(std::string(21, '-') + std::string(22, '_')).HasValue());
    EXPECT_FALSE(TokenSecret::Parse(std::string(43, '+')).HasValue());
    EXPECT_FALSE(TokenSecret::Parse(std::string(43, '=')).HasValue());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: срок действия приглашений.
TEST(OneTimeToken, InvitationDiesWhenItsTimeComes) {
    const auto issued = testing::FakeClock::DefaultStart();
    const auto invitation = Invitation(issued, 48h);

    EXPECT_TRUE(invitation.IsUsableAt(issued));
    EXPECT_TRUE(invitation.IsUsableAt(issued + Of(47h)));
    EXPECT_FALSE(invitation.IsUsableAt(issued + Of(48h)))
        << "приглашение годится ровно до момента истечения, а не включая его";
    EXPECT_FALSE(invitation.IsUsableAt(issued + Of(49h)));
}

TEST(OneTimeToken, ExpiredInvitationRefusesToWork) {
    const auto issued = testing::FakeClock::DefaultStart();
    const auto invitation = Invitation(issued, 48h);

    const auto late = invitation.Used(issued + Of(72h));

    ASSERT_FALSE(late.HasValue());
    EXPECT_EQ(late.Failure().Code(), "token_expired");
    EXPECT_EQ(late.Failure().Kind(), core::ErrorKind::kConflict);
}

TEST(OneTimeToken, ForwardedLinkDoesNotLetTheSecondIn) {
    const auto issued = testing::FakeClock::DefaultStart();
    const auto invitation = Invitation(issued);

    const auto first = invitation.Used(issued + Of(1h));
    ASSERT_TRUE(first.HasValue());
    EXPECT_FALSE(first.Value().IsUsableAt(issued + Of(2h)));

    const auto second = first.Value().Used(issued + Of(2h));
    ASSERT_FALSE(second.HasValue());
    EXPECT_EQ(second.Failure().Code(), "token_already_used");
}

TEST(OneTimeToken, LifetimeThatIsAlreadyOverIsRefused) {
    const auto refused = OneTimeToken::Invitation(Numbered<TokenId>(1),
                                                  Numbered<core::TenantId>(1),
                                                  SomeSecret(),
                                                  Role::kStudent,
                                                  std::nullopt,
                                                  testing::FakeClock::DefaultStart(),
                                                  core::Instant::Duration::zero());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "token_lifetime_not_positive");
}

/// Приглашение зовёт РОЛЬ, сброс указывает на ЧЕЛОВЕКА. Одновременно оба не
/// бывают — то же ограничение стоит в схеме.
TEST(OneTimeToken, EachPurposePointsAtOneThing) {
    const auto at = testing::FakeClock::DefaultStart();
    const auto invitation = Invitation(at);

    EXPECT_EQ(invitation.Purpose(), TokenPurpose::kInvitation);
    EXPECT_TRUE(invitation.InvitedAs().has_value());
    EXPECT_FALSE(invitation.Person().has_value());

    const auto reset = OneTimeToken::PasswordReset(Numbered<TokenId>(2),
                                                   Numbered<core::TenantId>(1),
                                                   SomeSecret(2),
                                                   Numbered<core::PersonId>(20),
                                                   at,
                                                   Of(1h))
                           .Value();

    EXPECT_EQ(reset.Purpose(), TokenPurpose::kPasswordReset);
    EXPECT_FALSE(reset.InvitedAs().has_value());
    EXPECT_TRUE(reset.Person().has_value());
}

TEST(OneTimeToken, PurposeNamesAreTheWordsTheDatabaseKnows) {
    for (const auto purpose : {TokenPurpose::kInvitation, TokenPurpose::kPasswordReset}) {
        const auto parsed = ParseTokenPurpose(Name(purpose));

        ASSERT_TRUE(parsed.has_value()) << "вид «" << Name(purpose) << "» не читается обратно";
        EXPECT_EQ(*parsed, purpose);
    }
    EXPECT_FALSE(ParseTokenPurpose("magic_link").has_value());
}

TEST(AuthLifetimes, ResetLinkNeverOutlivesInvitation) {
    const auto refused = AuthLifetimes::Compose(Of(24h), Of(1h), Of(2h));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "auth_reset_outlives_invitation");
}

}  // namespace
}  // namespace pdr::identity
