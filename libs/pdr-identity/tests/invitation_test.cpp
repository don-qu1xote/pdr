#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "builders/auth_world.hpp"
#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "identity/application/accept_invitation.hpp"
#include "identity/application/authenticate_session.hpp"
#include "identity/application/invite_participant.hpp"
#include "identity/application/reset_password.hpp"
#include "identity/application/sign_in.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

const auto kTenant = Numbered<core::TenantId>(1);
constexpr std::string_view kPassword = "длинный-пароль-1";

class InvitationTest : public ::testing::Test {
protected:
    InvitationTest()
        : mail_{Email::Parse("student@example.test").Value()},
          zone_{core::TimeZone::Parse("Europe/Moscow").value()},
          born_{BirthDate::Of(2011, 3, 4).Value()},
          seen_{digests_.Of("Chrome/1.0"), digests_.Of("192.0.2.10")} {}

    InviteParticipant Inviting() {
        return InviteParticipant{settings_, digests_, tokens_, ids_, secrets_, clock_};
    }

    AcceptInvitation Accepting() {
        return AcceptInvitation{settings_,
                                digests_,
                                hasher_,
                                tokens_,
                                accounts_,
                                directory_,
                                credentials_,
                                sessions_,
                                ids_,
                                secrets_,
                                clock_};
    }

    AcceptInvitationRequest Coming(const TokenSecret& secret) const {
        return AcceptInvitationRequest{
            kTenant, secret, "Ученик Петров", mail_, born_, zone_, std::string{kPassword}, seen_};
    }

    testing::FakeAccounts accounts_;
    testing::FakeDigests digests_;
    testing::FakeHasher hasher_;
    testing::FakeCredentials credentials_;
    testing::FakeSessions sessions_;
    testing::FakeAttempts attempts_;
    testing::FakeTokens tokens_;
    testing::FakeDirectory directory_{credentials_};
    testing::FakeSettings settings_;
    pdr::testing::FakeIdGenerator ids_;
    pdr::testing::FakeSecretGenerator secrets_;
    pdr::testing::FakeClock clock_;
    Email mail_;
    core::TimeZone zone_;
    BirthDate born_;
    Fingerprint seen_;
};

TEST_F(InvitationTest, TutorInvitesAndTheStudentComesIn) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(issued.HasValue()) << issued.Failure().Code();
    EXPECT_EQ(issued.Value().token.InvitedAs(), Role::kStudent);

    const auto opened = Accepting().Execute(Coming(issued.Value().secret));
    ASSERT_TRUE(opened.HasValue()) << opened.Failure().Code();

    ASSERT_EQ(directory_.Enrolled().size(), 1U);
    EXPECT_TRUE(directory_.Enrolled().front().roles.Has(Role::kStudent));
    EXPECT_EQ(directory_.Enrolled().front().person.Mail(), mail_);
    EXPECT_EQ(directory_.Enrolled().front().display_name, "Ученик Петров");

    const AuthenticateSession check{sessions_, clock_};
    EXPECT_TRUE(check.Execute(opened.Value().Id()).HasValue())
        << "пришедший по ссылке обязан оказаться внутри, а не на форме входа";
}

TEST_F(InvitationTest, TheStudentCanThenSignInWithThatPassword) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(Accepting().Execute(Coming(issued.Value().secret)).HasValue());

    const SignIn entering{
        settings_, credentials_, hasher_, digests_, attempts_, sessions_, secrets_, clock_};
    const auto opened = entering.Execute(
        SignInRequest{kTenant, mail_, std::string{kPassword}, seen_, std::nullopt});

    ASSERT_TRUE(opened.HasValue()) << opened.Failure().Code();
}

TEST_F(InvitationTest, ForwardedLinkDoesNotLetTheSecondIn) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(Accepting().Execute(Coming(issued.Value().secret)).HasValue());

    const auto second = Accepting().Execute(Coming(issued.Value().secret));

    ASSERT_FALSE(second.HasValue());
    EXPECT_EQ(second.Failure().Code(), "token_already_used");
    EXPECT_EQ(directory_.Enrolled().size(), 1U);
}

TEST_F(InvitationTest, AnInvitationThatSatTooLongIsRefused) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(issued.HasValue());

    clock_.Advance(settings_.Lifetimes().Value().Invitation());

    const auto late = Accepting().Execute(Coming(issued.Value().secret));

    ASSERT_FALSE(late.HasValue());
    EXPECT_EQ(late.Failure().Code(), "token_expired");
    EXPECT_TRUE(directory_.Enrolled().empty());
}

TEST_F(InvitationTest, ALinkNobodyIssuedIsUnknown) {
    const auto invented = TokenSecret::Parse(std::string(43, 'a')).Value();

    const auto refused = Accepting().Execute(Coming(invented));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "invitation_unknown");
}

TEST_F(InvitationTest, ATakenMailStopsTheEnrolmentAndKeepsTheLinkAlive) {
    const auto first = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(Accepting().Execute(Coming(first.Value().secret)).HasValue());

    const auto second = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    const auto refused = Accepting().Execute(Coming(second.Value().secret));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "participant_email_taken");

    EXPECT_TRUE(
        tokens_.Find(kTenant, digests_.Of(second.Value().secret.Value()))->IsUsableAt(clock_.Now()))
        << "ссылка сгорела на занятой почте: поправить её и прийти ещё раз уже нечем";
}

TEST_F(InvitationTest, ResetLinkGoesOnlyToSomeoneWhoExists) {
    const RequestPasswordReset asking{
        settings_, credentials_, digests_, tokens_, ids_, secrets_, clock_};

    const auto nobody = asking.Execute(kTenant, mail_);

    ASSERT_TRUE(nobody.HasValue()) << "ответ обязан быть одинаковым, а не отказом";
    EXPECT_FALSE(nobody.Value().has_value()) << "ссылку послали в пустоту";
}

TEST_F(InvitationTest, ResettingThePasswordRevokesEverySession) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    const auto first = Accepting().Execute(Coming(issued.Value().secret));
    ASSERT_TRUE(first.HasValue());

    const RequestPasswordReset asking{
        settings_, credentials_, digests_, tokens_, ids_, secrets_, clock_};
    const auto link = asking.Execute(kTenant, mail_);
    ASSERT_TRUE(link.HasValue());
    ASSERT_TRUE(link.Value().has_value());

    const ResetPassword resetting{
        settings_, digests_, hasher_, tokens_, credentials_, sessions_, secrets_, clock_};
    const auto fresh = resetting.Execute(
        ResetPasswordRequest{kTenant, link.Value()->secret, "совсем-другой-пароль", seen_});

    ASSERT_TRUE(fresh.HasValue()) << fresh.Failure().Code();

    const AuthenticateSession check{sessions_, clock_};
    EXPECT_FALSE(check.Execute(first.Value().Id()).HasValue())
        << "сброс пароля не погасил старую сессию";
    EXPECT_TRUE(check.Execute(fresh.Value().Id()).HasValue());
}

TEST_F(InvitationTest, ResetLinkDiesQuickly) {
    const auto issued = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(Accepting().Execute(Coming(issued.Value().secret)).HasValue());

    const RequestPasswordReset asking{
        settings_, credentials_, digests_, tokens_, ids_, secrets_, clock_};
    const auto link = asking.Execute(kTenant, mail_);
    ASSERT_TRUE(link.Value().has_value());

    clock_.Advance(settings_.Lifetimes().Value().PasswordReset());

    const ResetPassword resetting{
        settings_, digests_, hasher_, tokens_, credentials_, sessions_, secrets_, clock_};
    const auto late = resetting.Execute(
        ResetPasswordRequest{kTenant, link.Value()->secret, "совсем-другой-пароль", seen_});

    ASSERT_FALSE(late.HasValue());
    EXPECT_EQ(late.Failure().Code(), "token_expired");
}

/// Приглашение и сброс — один механизм, но не одна ссылка: приглашением нельзя
/// сбросить пароль, а ссылкой сброса — завести человека.
TEST_F(InvitationTest, OneKindOfLinkDoesNotOpenTheOtherDoor) {
    const auto invitation = Inviting().Execute(kTenant, Role::kStudent, std::nullopt);
    ASSERT_TRUE(invitation.HasValue());

    const ResetPassword resetting{
        settings_, digests_, hasher_, tokens_, credentials_, sessions_, secrets_, clock_};
    const auto refused = resetting.Execute(
        ResetPasswordRequest{kTenant, invitation.Value().secret, "совсем-другой-пароль", seen_});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "password_reset_unknown");
}

}  // namespace
}  // namespace pdr::identity
