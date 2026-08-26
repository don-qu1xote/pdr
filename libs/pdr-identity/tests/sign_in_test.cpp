#include "identity/application/sign_in.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "builders/auth_world.hpp"
#include "builders/identifiers.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "identity/application/authenticate_session.hpp"
#include "identity/application/change_password.hpp"
#include "identity/application/sign_out.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant::Duration Of(std::chrono::minutes minutes) {
    return std::chrono::duration_cast<core::Instant::Duration>(minutes);
}

const auto kTenant = Numbered<core::TenantId>(1);
const auto kOther = Numbered<core::TenantId>(2);
const auto kStudent = Numbered<core::PersonId>(20);
constexpr std::string_view kSecret = "верный-пароль-1";

class SignInTest : public ::testing::Test {
protected:
    SignInTest()
        : mail_{Email::Parse("student@example.test").Value()},
          seen_{digests_.Of("Chrome/1.0"), digests_.Of("192.0.2.10")} {
        credentials_.Put(
            kTenant,
            kStudent,
            mail_,
            hasher_.Hash(Password::Given(kSecret).Value(), settings_.Passwords().Value()).Value());
    }

    SignIn Scenario() {
        return SignIn{
            settings_, credentials_, hasher_, digests_, attempts_, sessions_, secrets_, clock_};
    }

    SignInRequest Request(std::string_view secret = kSecret) const {
        return SignInRequest{kTenant, mail_, std::string{secret}, seen_, std::nullopt};
    }

    testing::FakeDigests digests_;
    testing::FakeHasher hasher_;
    testing::FakeCredentials credentials_;
    testing::FakeSessions sessions_;
    testing::FakeAttempts attempts_;
    testing::FakeSettings settings_;
    pdr::testing::FakeSecretGenerator secrets_;
    pdr::testing::FakeClock clock_;
    Email mail_;
    Fingerprint seen_;
};

TEST_F(SignInTest, RightPasswordOpensASession) {
    const auto opened = Scenario().Execute(Request());

    ASSERT_TRUE(opened.HasValue()) << opened.Failure().Code();
    EXPECT_EQ(opened.Value().Person(), kStudent);
    EXPECT_EQ(opened.Value().Tenant(), kTenant);
    EXPECT_TRUE(opened.Value().IsUsableAt(clock_.Now()));
    EXPECT_EQ(opened.Value().Id().Tenant(), kTenant)
        << "арендатор едет вместе с секретом: без него строку сессии не прочитать";
}

TEST_F(SignInTest, WrongPasswordAndUnknownMailAreTheSameAnswer) {
    const auto wrong = Scenario().Execute(Request("не-тот-пароль"));

    SignInRequest unknown = Request();
    unknown.mail = Email::Parse("nobody@example.test").Value();
    const auto missing = Scenario().Execute(unknown);

    ASSERT_FALSE(wrong.HasValue());
    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(wrong.Failure().Code(), missing.Failure().Code())
        << "форма входа рассказывает, кто у нас учится";
    EXPECT_EQ(wrong.Failure().Code(), "sign_in_refused");
}

/// Неизвестная почта стоит столько же, сколько известная: без этого «такой
/// почты нет» отвечает мгновенно, и разницу видно из-за океана.
TEST_F(SignInTest, UnknownMailCostsAsMuchAsAKnownOne) {
    SignInRequest unknown = Request();
    unknown.mail = Email::Parse("nobody@example.test").Value();

    const auto before = hasher_.Counted();
    static_cast<void>(Scenario().Execute(unknown));
    const auto spent_on_unknown = hasher_.Counted() - before;

    const auto known_before = hasher_.Counted();
    static_cast<void>(Scenario().Execute(Request("не-тот-пароль")));
    const auto spent_on_known = hasher_.Counted() - known_before;

    EXPECT_EQ(spent_on_unknown, spent_on_known);
}

TEST_F(SignInTest, SomeoneElsesTenantIsNotOurs) {
    SignInRequest elsewhere = Request();
    elsewhere.tenant = kOther;

    const auto refused = Scenario().Execute(elsewhere);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "sign_in_refused");
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: старый идентификатор после ротации не работает.
TEST_F(SignInTest, TheIdentifierBroughtToTheFormDiesAtTheDoor) {
    const auto first = Scenario().Execute(Request());
    ASSERT_TRUE(first.HasValue());
    const auto planted = first.Value().Id();

    SignInRequest again = Request();
    again.current = planted;
    const auto second = Scenario().Execute(again);
    ASSERT_TRUE(second.HasValue());

    EXPECT_NE(second.Value().Id().ToString(), planted.ToString())
        << "идентификатор не сменился: подсунутый до входа работает и после";

    const AuthenticateSession check{sessions_, clock_};
    const auto old_one = check.Execute(planted);

    ASSERT_FALSE(old_one.HasValue());
    EXPECT_EQ(old_one.Failure().Code(), "session_revoked");
    EXPECT_TRUE(check.Execute(second.Value().Id()).HasValue());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: вход — выход — вход.
TEST_F(SignInTest, SignInSignOutSignInAgain) {
    const AuthenticateSession check{sessions_, clock_};

    const auto first = Scenario().Execute(Request());
    ASSERT_TRUE(first.HasValue());
    ASSERT_TRUE(check.Execute(first.Value().Id()).HasValue());

    const SignOut out{sessions_, clock_};
    ASSERT_TRUE(out.Execute(first.Value().Id()).HasValue());
    EXPECT_FALSE(check.Execute(first.Value().Id()).HasValue())
        << "после выхода сессия продолжает работать";

    const auto second = Scenario().Execute(Request());
    ASSERT_TRUE(second.HasValue());
    EXPECT_TRUE(check.Execute(second.Value().Id()).HasValue());
    EXPECT_NE(second.Value().Id().ToString(), first.Value().Id().ToString());
}

TEST_F(SignInTest, LeavingTwiceIsNotAnError) {
    const auto opened = Scenario().Execute(Request());
    ASSERT_TRUE(opened.HasValue());

    const SignOut out{sessions_, clock_};
    EXPECT_TRUE(out.Execute(opened.Value().Id()).HasValue());
    EXPECT_TRUE(out.Execute(opened.Value().Id()).HasValue())
        << "второе нажатие «выйти» обязано выглядеть как первое";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: превышение порога даёт отказ и снимается по
/// истечении окна.
TEST_F(SignInTest, TooManyMissesLockTheDoorAndTheWindowOpensIt) {
    const auto limits = settings_.Throttle().Value();

    for (std::uint32_t miss = 0; miss < limits.For(AttemptSubject::kAccount); ++miss) {
        const auto refused = Scenario().Execute(Request("не-тот-пароль"));
        ASSERT_FALSE(refused.HasValue());
        EXPECT_EQ(refused.Failure().Code(), "sign_in_refused");
    }

    const auto locked = Scenario().Execute(Request());
    ASSERT_FALSE(locked.HasValue()) << "верный пароль прошёл поверх запрета";
    EXPECT_EQ(locked.Failure().Code(), "sign_in_throttled");
    EXPECT_EQ(locked.Failure().Kind(), core::ErrorKind::kConflict);

    clock_.Advance(limits.Window());

    const auto opened = Scenario().Execute(Request());
    ASSERT_TRUE(opened.HasValue()) << "окно кончилось, а запрет остался";
}

TEST_F(SignInTest, SuccessForgetsTheCount) {
    for (int miss = 0; miss < 2; ++miss) {
        static_cast<void>(Scenario().Execute(Request("не-тот-пароль")));
    }
    ASSERT_TRUE(Scenario().Execute(Request()).HasValue());

    for (int miss = 0; miss < 2; ++miss) {
        static_cast<void>(Scenario().Execute(Request("не-тот-пароль")));
    }

    EXPECT_TRUE(Scenario().Execute(Request()).HasValue())
        << "удачный вход не сбросил счёт: при пороге в три попытки ещё две неудачи заперли "
           "вход, и человек доживает окно вместе с тем, кто его перебирал";
}

/// Смена пароля гасит ВСЁ, включая ту сессию, из которой пришли, и выдаёт
/// новую. Унесённое устройство перестаёт работать — ради этого пароль и меняют.
TEST_F(SignInTest, ChangingThePasswordRevokesEveryOldIdentifier) {
    const auto laptop = Scenario().Execute(Request());
    const auto phone = Scenario().Execute(Request());
    ASSERT_TRUE(laptop.HasValue());
    ASSERT_TRUE(phone.HasValue());

    const ChangePassword change{settings_, credentials_, hasher_, sessions_, secrets_, clock_};
    const auto fresh = change.Execute(ChangePasswordRequest{
        laptop.Value().Id(), std::string{kSecret}, "новый-длинный-пароль", seen_});

    ASSERT_TRUE(fresh.HasValue()) << fresh.Failure().Code();

    const AuthenticateSession check{sessions_, clock_};
    EXPECT_FALSE(check.Execute(laptop.Value().Id()).HasValue());
    EXPECT_FALSE(check.Execute(phone.Value().Id()).HasValue())
        << "унесённое устройство продолжает работать после смены пароля";
    EXPECT_TRUE(check.Execute(fresh.Value().Id()).HasValue());
}

TEST_F(SignInTest, ChangingThePasswordNeedsTheOldOne) {
    const auto opened = Scenario().Execute(Request());
    ASSERT_TRUE(opened.HasValue());

    const ChangePassword change{settings_, credentials_, hasher_, sessions_, secrets_, clock_};
    const auto refused = change.Execute(
        ChangePasswordRequest{opened.Value().Id(), "чужая-догадка", "новый-длинный-пароль", seen_});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "password_old_mismatch");
}

TEST_F(SignInTest, ANewPasswordObeysTheRules) {
    const auto opened = Scenario().Execute(Request());
    ASSERT_TRUE(opened.HasValue());

    const ChangePassword change{settings_, credentials_, hasher_, sessions_, secrets_, clock_};
    const auto refused = change.Execute(
        ChangePasswordRequest{opened.Value().Id(), std::string{kSecret}, "коротко", seen_});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "password_too_short");
}

TEST_F(SignInTest, ExpiredSessionStopsWorkingOnItsOwn) {
    const auto opened = Scenario().Execute(Request());
    ASSERT_TRUE(opened.HasValue());

    clock_.Advance(settings_.Lifetimes().Value().Session());

    const AuthenticateSession check{sessions_, clock_};
    const auto stale = check.Execute(opened.Value().Id());

    ASSERT_FALSE(stale.HasValue());
    EXPECT_EQ(stale.Failure().Code(), "session_expired");
}

TEST_F(SignInTest, AnIdentifierNobodyIssuedIsUnknown) {
    const AuthenticateSession check{sessions_, clock_};
    const auto nothing = check.Execute(SessionId{kTenant, Numbered<SessionSecret>(999)});

    ASSERT_FALSE(nothing.HasValue());
    EXPECT_EQ(nothing.Failure().Code(), "session_unknown");
}

}  // namespace
}  // namespace pdr::identity
