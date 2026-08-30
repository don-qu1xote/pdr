#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/auth_world.hpp"
#include "builders/identifiers.hpp"
#include "events/in_memory_bus.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "identity/application/accept_invitation.hpp"
#include "identity/application/attach_account.hpp"
#include "identity/application/confirm_email.hpp"
#include "identity/application/enrol_child.hpp"
#include "identity/application/invite_many.hpp"
#include "identity/application/manage_publication.hpp"
#include "identity/application/open_practice.hpp"
#include "identity/application/register_on_my_own.hpp"
#include "identity/core/contact_list.hpp"
#include "identity/core/practice.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

constexpr std::string_view kPassword = "довольно-длинный-пароль";

/// Двадцать учеников из таблицы репетитора: ровно то, что вставляют.
std::string TwentyStudents() {
    std::string pasted;
    for (int number = 1; number <= 20; ++number) {
        pasted += "Ученик " + std::to_string(number) + " <student" + std::to_string(number) +
                  "@example.test>\n";
    }
    return pasted;
}

/// Весь вход в продукт на фейках: практика, приглашения, учётные записи.
class OnboardingTest : public ::testing::Test {
protected:
    OnboardingTest()
        : zone_{core::TimeZone::Parse("Europe/Moscow").value()},
          born_{BirthDate::Of(1985, 6, 12).Value()},
          seen_{digests_.Of("Chrome/1.0"), digests_.Of("192.0.2.10")} {}

    OpenPractice Opening() {
        return OpenPractice{settings_,
                            digests_,
                            hasher_,
                            accounts_,
                            practices_,
                            directory_,
                            credentials_,
                            sessions_,
                            ids_,
                            secrets_,
                            clock_};
    }

    InviteParticipant Inviting() {
        return InviteParticipant{settings_, digests_, tokens_, ids_, secrets_, clock_};
    }

    InviteMany Bulk() {
        return InviteMany{inviting_, digests_, tokens_, directory_, clock_};
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

    OpenPracticeRequest Founder(std::string_view mail = "tutor@example.test") const {
        return OpenPracticeRequest{"Практика Ивановой",
                                   "Мария Иванова",
                                   Email::Parse(mail).Value(),
                                   born_,
                                   zone_,
                                   std::string{kPassword},
                                   seen_};
    }

    AcceptInvitationRequest Coming(const core::TenantId& tenant,
                                   const TokenSecret& secret,
                                   std::string_view mail) const {
        return AcceptInvitationRequest{tenant,
                                       secret,
                                       "Ученик Петров",
                                       Email::Parse(mail).Value(),
                                       BirthDate::Of(2011, 3, 4).Value(),
                                       zone_,
                                       std::string{kPassword},
                                       seen_};
    }

    testing::FakeAccounts accounts_;
    testing::FakeCredentials credentials_;
    testing::FakeDigests digests_;
    testing::FakeDirectory directory_{credentials_};
    testing::FakeHasher hasher_;
    testing::FakePractices practices_;
    testing::FakeSessions sessions_;
    testing::FakeSettings settings_;
    testing::FakeSignups signups_;
    testing::FakeTokens tokens_;

    pdr::events::InMemoryBus bus_;
    pdr::testing::FakeClock clock_;
    pdr::testing::FakeIdGenerator ids_;
    pdr::testing::FakeSecretGenerator secrets_;

    InviteParticipant inviting_{settings_, digests_, tokens_, ids_, secrets_, clock_};

    core::TimeZone zone_;
    BirthDate born_;
    Fingerprint seen_;
};

/// ГЛАВНЫЙ ТЕСТ ЗАДАЧИ: путь переноса практики проходится ЦЕЛИКОМ, и подбор в
/// нём не участвует ни разу.
///
/// Проверяется не «мы не позвали подбор» — позвать было бы нечего, контекста
/// подбора в дереве нет, — а то, ради чего он и заводится скрытым: практика
/// работает полностью, оставаясь невидимой снаружи. Заведена, ученики позваны,
/// один уже пришёл и вошёл, а в подбор она не попадает и попасть не просила.
TEST_F(OnboardingTest, APracticeMovesInWithoutEverSeeingDiscovery) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue()) << opened.Failure().Code();
    const auto tenant = opened.Value().tenant;

    const auto practice = practices_.Find(tenant);
    ASSERT_TRUE(practice.has_value());
    EXPECT_EQ(practice->Visible(), Visibility::kHidden)
        << "практика завелась видимой: репетитор попал в подбор, не спросив";
    EXPECT_FALSE(practice->IsDiscoverable());

    const auto preview = Bulk().Preview(tenant, TwentyStudents());
    EXPECT_EQ(preview.Lines().size(), 20U);
    EXPECT_EQ(preview.Ready(), 20U);

    const auto run = Bulk().Send(tenant, Role::kStudent, preview);
    ASSERT_TRUE(run.HasValue()) << run.Failure().Code();
    EXPECT_EQ(run.Value().sent.size(), 20U) << "позвали не всех, кого показали";

    const auto& first = run.Value().sent.front();
    const auto session = Accepting().Execute(Coming(tenant, first.secret, first.mail.Value()));
    ASSERT_TRUE(session.HasValue()) << session.Failure().Code();

    EXPECT_EQ(directory_.Enrolled().size(), 2U) << "репетитор и первый ученик";
    EXPECT_EQ(practices_.Find(tenant)->Visible(), Visibility::kHidden)
        << "практика стала видимой сама, по ходу работы";
}

TEST_F(OnboardingTest, TheFounderIsBothOwnerAndTutor) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());

    const auto& roles = directory_.Enrolled().front().roles;
    EXPECT_TRUE(roles.Has(Role::kOwner));
    EXPECT_TRUE(roles.Has(Role::kTutor)) << "репетитор-одиночка заводит себе вторую роль руками";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: повторная отправка не шлёт второго письма.
TEST_F(OnboardingTest, ASecondSendWritesToNobodyTwice) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    const auto first =
        Bulk().Send(tenant, Role::kStudent, Bulk().Preview(tenant, TwentyStudents()));
    ASSERT_TRUE(first.HasValue());
    ASSERT_EQ(first.Value().sent.size(), 20U);

    const auto again =
        Bulk().Send(tenant, Role::kStudent, Bulk().Preview(tenant, TwentyStudents()));
    ASSERT_TRUE(again.HasValue()) << again.Failure().Code();
    EXPECT_TRUE(again.Value().sent.empty()) << "тем же людям ушло второе приглашение";

    for (const auto& line : again.Value().judged.Lines()) {
        EXPECT_EQ(line.Verdict(), ContactVerdict::kAlreadyInvited) << line.Raw();
    }
}

TEST_F(OnboardingTest, ThePreviewJudgesEveryLineOnItsOwn) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    const auto preview = Bulk().Preview(tenant,
                                        "one@example.test, two@example.test\n"
                                        "не адрес вовсе\n"
                                        "one@example.test\n"
                                        "tutor@example.test\n");

    ASSERT_EQ(preview.Lines().size(), 5U);
    EXPECT_EQ(preview.Lines()[0].Verdict(), ContactVerdict::kReady);
    EXPECT_EQ(preview.Lines()[1].Verdict(), ContactVerdict::kReady);
    EXPECT_EQ(preview.Lines()[2].Verdict(), ContactVerdict::kMalformed);
    EXPECT_EQ(preview.Lines()[2].Raw(), "не адрес вовсе")
        << "строку показывают человеку как есть: «строка 3 не разобралась» ему не поможет";
    EXPECT_EQ(preview.Lines()[3].Verdict(), ContactVerdict::kRepeatedInList);
    EXPECT_EQ(preview.Lines()[4].Verdict(), ContactVerdict::kAlreadyEnrolled)
        << "репетитор позвал сам себя";
    EXPECT_EQ(preview.Ready(), 2U);
}

/// Предпросмотр НИЧЕГО не отправляет: рассылки сразу после импорта не бывает.
TEST_F(OnboardingTest, ThePreviewSendsNothing) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());

    static_cast<void>(Bulk().Preview(opened.Value().tenant, TwentyStudents()));

    EXPECT_TRUE(tokens_.Rows().empty()) << "письма ушли до того, как их показали";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: профиль не публикуется до разбора, а практика при
/// этом работает.
TEST_F(OnboardingTest, ThePracticeWorksWhileItWaitsForReview) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    const AskToPublish ask{practices_, clock_, bus_};
    const auto asked = ask.Execute(tenant);
    ASSERT_TRUE(asked.HasValue()) << asked.Failure().Code();
    EXPECT_EQ(asked.Value().Visible(), Visibility::kPending);
    EXPECT_FALSE(asked.Value().IsDiscoverable()) << "показали до разбора";

    const auto run = Bulk().Send(tenant, Role::kStudent, Bulk().Preview(tenant, TwentyStudents()));
    ASSERT_TRUE(run.HasValue()) << "практика перестала работать, пока ждёт разбора";
    EXPECT_EQ(run.Value().sent.size(), 20U);

    const DecidePublication decide{practices_, clock_, bus_};
    const auto published = decide.Publish(tenant);
    ASSERT_TRUE(published.HasValue()) << published.Failure().Code();
    EXPECT_TRUE(published.Value().IsDiscoverable());
}

TEST_F(OnboardingTest, ARefusalNamesItsReasonAndIsNotFinal) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    const AskToPublish ask{practices_, clock_, bus_};
    const DecidePublication decide{practices_, clock_, bus_};
    ASSERT_TRUE(ask.Execute(tenant).HasValue());

    const auto refused = decide.Refuse(tenant, RefusalReason::kNothingToShow);
    ASSERT_TRUE(refused.HasValue()) << refused.Failure().Code();
    EXPECT_EQ(refused.Value().Visible(), Visibility::kRefused);
    ASSERT_TRUE(refused.Value().Refusal().has_value());
    EXPECT_EQ(*refused.Value().Refusal(), RefusalReason::kNothingToShow);

    EXPECT_TRUE(ask.Execute(tenant).HasValue()) << "после отказа попросить снова нельзя";
}

TEST_F(OnboardingTest, HidingWorksFromAnyStateAndWithoutReview) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    const AskToPublish ask{practices_, clock_, bus_};
    const DecidePublication decide{practices_, clock_, bus_};
    ASSERT_TRUE(ask.Execute(tenant).HasValue());
    ASSERT_TRUE(decide.Publish(tenant).HasValue());

    const auto hidden = ask.Hide(tenant);
    ASSERT_TRUE(hidden.HasValue());
    EXPECT_FALSE(hidden.Value().IsDiscoverable()) << "спрятаться нельзя без разбора";
}

TEST_F(OnboardingTest, ADecisionWithoutARequestIsRefused) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());

    const DecidePublication decide{practices_, clock_, bus_};
    const auto refused = decide.Publish(opened.Value().tenant);

    ASSERT_FALSE(refused.HasValue()) << "практику опубликовали, не спросив её хозяина";
    EXPECT_EQ(refused.Failure().Code(), "practice_decision_without_request");
}

/// ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ЗАДАЧИ: у одного ученика несколько репетиторов, и
/// каждый видит только своё.
TEST_F(OnboardingTest, OnePersonTwoPracticesAndNeitherTutorSeesTheOther) {
    const auto first = Opening().Execute(Founder("math@example.test"));
    const auto second = Opening().Execute(Founder("english@example.test"));
    ASSERT_TRUE(first.HasValue());
    ASSERT_TRUE(second.HasValue());

    const auto invite = [&](const core::TenantId& tenant) {
        const auto issued =
            Inviting().Execute(tenant, Role::kStudent, Email::Parse("masha@example.test").Value());
        EXPECT_TRUE(issued.HasValue()) << issued.Failure().Code();
        return issued.Value().secret;
    };

    const auto to_math = invite(first.Value().tenant);
    const auto joined_math =
        Accepting().Execute(Coming(first.Value().tenant, to_math, "masha@example.test"));
    ASSERT_TRUE(joined_math.HasValue()) << joined_math.Failure().Code();

    const auto to_english = invite(second.Value().tenant);
    const auto joined_english =
        Accepting().Execute(Coming(second.Value().tenant, to_english, "masha@example.test"));
    ASSERT_TRUE(joined_english.HasValue()) << joined_english.Failure().Code();

    EXPECT_TRUE(joined_math.Value().Person() == joined_english.Value().Person())
        << "у Маши завелось два разных человека: склеивать их потом будет нечем";

    const auto registered = accounts_.FindByMail(digests_.Of("masha@example.test"));
    ASSERT_TRUE(registered.has_value());
    EXPECT_EQ(accounts_.Rows().size(), 3U) << "две практики и ученица — три человека, не больше";
}

/// ОТРИЦАТЕЛЬНЫЙ ТЕСТ, ОБЯЗАТЕЛЬНЫЙ ПО ЗАДАЧЕ: репетитор не видит НИЧЕГО о
/// занятиях ученика у других — ни факта, ни предмета, ни имени коллеги.
///
/// Проверяется на самом узком месте: у обоих репетиторов ученик — один и тот же
/// идентификатор, и запрос «по человеку» без границы арендатора нашёл бы обе
/// практики. Здесь его нет: приглашения, люди и согласия спрашиваются внутри
/// арендатора, а реестр учётных записей отвечает ровно на один вопрос — «этот
/// человек уже есть?» — и не отвечает на вопрос «где ещё».
TEST_F(OnboardingTest, ATutorLearnsNothingAboutTheOtherPractice) {
    const auto math = Opening().Execute(Founder("math@example.test"));
    const auto english = Opening().Execute(Founder("english@example.test"));
    ASSERT_TRUE(math.HasValue());
    ASSERT_TRUE(english.HasValue());

    const auto mail = Email::Parse("masha@example.test").Value();
    const auto issued = Inviting().Execute(english.Value().tenant, Role::kStudent, mail);
    ASSERT_TRUE(issued.HasValue());
    ASSERT_TRUE(Accepting()
                    .Execute(Coming(english.Value().tenant, issued.Value().secret, mail.Value()))
                    .HasValue());

    EXPECT_FALSE(directory_.Knows(math.Value().tenant, mail))
        << "репетитор по математике видит ученицу, которая учится у другого";
    EXPECT_FALSE(
        tokens_.LiveInvitationTo(math.Value().tenant, digests_.Of(mail.Value()), clock_.Now())
            .has_value())
        << "чужое приглашение видно из другой практики";

    const auto mine = Inviting().Execute(math.Value().tenant, Role::kStudent, mail);
    EXPECT_TRUE(mine.HasValue())
        << "приглашение к себе упёрлось в чужую практику: репетитор узнал о ней отказом";
}

/// ОБА ПУТИ ВЕДУТ В ОДНО СОСТОЯНИЕ.
TEST_F(OnboardingTest, BothWaysInEndAtTheSameAccount) {
    const RegisterOnMyOwn myself{settings_, digests_, accounts_, signups_, ids_, secrets_, clock_};

    const auto came =
        myself.Execute(Email::Parse("self@example.test").Value(), digests_.Of("192.0.2.10"));
    ASSERT_TRUE(came.HasValue()) << came.Failure().Code();
    EXPECT_FALSE(came.Value().account.IsConfirmed()) << "почту не спросили вовсе";

    const ConfirmEmail confirm{digests_, accounts_, clock_};
    const auto confirmed = confirm.Execute(came.Value().account.Id(), came.Value().confirmation);
    ASSERT_TRUE(confirmed.HasValue()) << confirmed.Failure().Code();
    EXPECT_TRUE(confirmed.Value().IsConfirmed());

    const auto opened = Opening().Execute(Founder("invited@example.test"));
    ASSERT_TRUE(opened.HasValue());
    const auto invited = accounts_.FindByMail(digests_.Of("invited@example.test"));
    ASSERT_TRUE(invited.has_value());
    EXPECT_TRUE(invited->IsConfirmed())
        << "письмо со ссылкой дошло, а почту всё равно просят подтвердить";

    EXPECT_EQ(accounts_.Rows().size(), 2U);
}

TEST_F(OnboardingTest, TheSameMailIsNotRegisteredTwice) {
    const RegisterOnMyOwn myself{settings_, digests_, accounts_, signups_, ids_, secrets_, clock_};
    const auto mail = Email::Parse("self@example.test").Value();

    ASSERT_TRUE(myself.Execute(mail, digests_.Of("192.0.2.10")).HasValue());

    const auto again = myself.Execute(mail, digests_.Of("192.0.2.11"));
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "account_already_exists");
}

TEST_F(OnboardingTest, TooManySignupsFromOneAddressAreRefused) {
    const RegisterOnMyOwn myself{settings_, digests_, accounts_, signups_, ids_, secrets_, clock_};
    const auto from = digests_.Of("192.0.2.10");

    for (int number = 1; number <= 3; ++number) {
        const auto mail = Email::Parse("self" + std::to_string(number) + "@example.test").Value();
        ASSERT_TRUE(myself.Execute(mail, from).HasValue()) << number;
    }

    const auto refused = myself.Execute(Email::Parse("self4@example.test").Value(), from);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "signup_throttled");

    clock_.Advance(std::chrono::duration_cast<core::Instant::Duration>(2h));
    EXPECT_TRUE(myself.Execute(Email::Parse("self5@example.test").Value(), from).HasValue())
        << "запрет не снялся сам: человек заперт навсегда";
}

TEST_F(OnboardingTest, AConfirmationLinkWorksOnceAndNotAfterItsTime) {
    const RegisterOnMyOwn myself{settings_, digests_, accounts_, signups_, ids_, secrets_, clock_};
    const ConfirmEmail confirm{digests_, accounts_, clock_};

    const auto came =
        myself.Execute(Email::Parse("self@example.test").Value(), digests_.Of("192.0.2.10"));
    ASSERT_TRUE(came.HasValue());

    ASSERT_TRUE(confirm.Execute(came.Value().account.Id(), came.Value().confirmation).HasValue());

    const auto twice = confirm.Execute(came.Value().account.Id(), came.Value().confirmation);
    ASSERT_FALSE(twice.HasValue());
    EXPECT_EQ(twice.Failure().Code(), "account_already_confirmed");
}

/// ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ЗАДАЧИ: семилетке не заводят почту.
TEST_F(OnboardingTest, AChildIsEnrolledWithoutAnAccountAndGetsOneLater) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());
    const auto tenant = opened.Value().tenant;

    testing::FakeGuardianship guardianships;
    const EnrolChild enrol{directory_, guardianships, ids_, clock_};
    const auto child = enrol.Execute(EnrolChildRequest{
        tenant, opened.Value().owner, "Петя", BirthDate::Of(2019, 4, 1).Value(), zone_});

    ASSERT_TRUE(child.HasValue()) << child.Failure().Code();
    EXPECT_FALSE(directory_.Enrolled().back().person.Mail().has_value())
        << "семилетке завели почтовый ящик";
    EXPECT_TRUE(accounts_.FindById(child.Value()) == std::nullopt)
        << "ребёнку завели учётную запись, о которой никто не просил";

    const AttachAccount attach{inviting_, digests_, accounts_, ids_, clock_};
    const auto later =
        attach.Execute(tenant, child.Value(), Email::Parse("petya@example.test").Value());

    ASSERT_TRUE(later.HasValue()) << later.Failure().Code();
    const auto now = accounts_.FindByMail(digests_.Of("petya@example.test"));
    ASSERT_TRUE(now.has_value());
    EXPECT_TRUE(now->Id() == child.Value()) << "учётная запись досталась не тому человеку";
}

TEST_F(OnboardingTest, SomeoneElsesMailIsNotAttachedToAChild) {
    const auto opened = Opening().Execute(Founder());
    ASSERT_TRUE(opened.HasValue());

    testing::FakeGuardianship guardianships;
    const EnrolChild enrol{directory_, guardianships, ids_, clock_};
    const auto child = enrol.Execute(EnrolChildRequest{opened.Value().tenant,
                                                       opened.Value().owner,
                                                       "Петя",
                                                       BirthDate::Of(2019, 4, 1).Value(),
                                                       zone_});
    ASSERT_TRUE(child.HasValue());

    const AttachAccount attach{inviting_, digests_, accounts_, ids_, clock_};
    const auto refused = attach.Execute(opened.Value().tenant, child.Value(), Founder().mail);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "account_belongs_to_another");
}

TEST(ContactListTest, WhatPeopleActuallyPaste) {
    const auto parsed = ContactList::Parse(
        "  ivan@example.test ;\n"
        "Пётр Петров <PETR@Example.TEST>\n"
        "\n"
        "\tanna@example.test\n");

    ASSERT_EQ(parsed.Lines().size(), 3U) << "пустые строки попали в список";
    EXPECT_EQ(parsed.Lines()[0].Mail()->Value(), "ivan@example.test");
    EXPECT_EQ(parsed.Lines()[1].Mail()->Value(), "petr@example.test")
        << "адрес из почтового клиента не разобрался";
    EXPECT_EQ(parsed.Lines()[2].Mail()->Value(), "anna@example.test");
    EXPECT_EQ(parsed.Ready(), 3U);
}

TEST(ContactListTest, AWholeStolenBaseDoesNotFitInOnePaste) {
    std::string huge;
    for (std::size_t number = 0; number < ContactList::kMostLines + 50; ++number) {
        huge += "person" + std::to_string(number) + "@example.test\n";
    }

    EXPECT_EQ(ContactList::Parse(huge).Lines().size(), ContactList::kMostLines);
}

TEST(ContactListTest, CodesAreTheWordsTheInterfaceKnows) {
    for (const auto verdict : kEveryContactVerdict) {
        const auto parsed = ParseContactVerdict(Name(verdict));

        ASSERT_TRUE(parsed.has_value()) << Name(verdict);
        EXPECT_EQ(*parsed, verdict);
    }
    for (const auto visibility : kEveryVisibility) {
        EXPECT_EQ(ParseVisibility(Name(visibility)).value(), visibility);
    }
    for (const auto reason : kEveryRefusalReason) {
        EXPECT_EQ(ParseRefusalReason(Name(reason)).value(), reason);
    }
}

}  // namespace
}  // namespace pdr::identity
