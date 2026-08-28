#include "identity/core/guardian_access.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "events/identity/guardian_handover_started.hpp"
#include "events/in_memory_bus.hpp"
#include "identity/application/announce_guardian_handover.hpp"
#include "identity/application/grant_guardian_scope.hpp"
#include "identity/application/revoke_guardian_scope.hpp"
#include "identity/application/show_access_journal.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::testing::Numbered;

core::Instant::Duration Days(int days) {
    return std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * days});
}

const auto kTenant = Numbered<core::TenantId>(1);
const auto kTutor = Numbered<core::PersonId>(10);
const auto kStudent = Numbered<core::PersonId>(20);
const auto kGuardian = Numbered<core::PersonId>(30);

MaturityRule Rule(int grace_days = 30) {
    return MaturityRule::Compose(AgeThresholds::Compose(14, 16, 18).Value(), Days(grace_days))
        .Value();
}

GuardianConsent Consent(GuardianScope scope,
                        core::PersonId granted_by,
                        core::Instant at,
                        int number = 1,
                        ConsentBasis basis = ConsentBasis::kGuardianship) {
    return GuardianConsent::Grant(Numbered<ConsentId>(static_cast<std::uint64_t>(number)),
                                  kTenant,
                                  kGuardian,
                                  kStudent,
                                  scope,
                                  basis,
                                  std::move(granted_by),
                                  at,
                                  std::nullopt)
        .Value();
}

TEST(GuardianScopes, RecordingsDoNotOpenWithGuardianship) {
    EXPECT_FALSE(OpensWithGuardianship(GuardianScope::kRecordings))
        << "запись занятия открылась вместе с опекой";
    EXPECT_TRUE(OpensWithGuardianship(GuardianScope::kSchedule));
    EXPECT_TRUE(OpensWithGuardianship(GuardianScope::kPayments));
    EXPECT_TRUE(OpensWithGuardianship(GuardianScope::kNotesAndHomework));
}

/// ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ЗАДАЧИ: опекун сохраняет расписание при любом возрасте
/// ДО совершеннолетия — и не после него.
TEST(GuardianScopes, ContentGoesToTheStudentEarlyAndTheRestAtMajority) {
    EXPECT_EQ(WhenStudentDecides(GuardianScope::kRecordings), AgeThreshold::kSlotsAndReviews);
    EXPECT_EQ(WhenStudentDecides(GuardianScope::kNotesAndHomework), AgeThreshold::kSlotsAndReviews);
    EXPECT_EQ(WhenStudentDecides(GuardianScope::kSchedule), AgeThreshold::kMajority)
        << "расписание оборвалось раньше совершеннолетия, посреди учебного года";
    EXPECT_EQ(WhenStudentDecides(GuardianScope::kPayments), AgeThreshold::kMajority);
}

TEST(GuardianScopes, CodesAreTheWordsTheDatabaseKnows) {
    for (const auto scope : kEveryGuardianScope) {
        const auto parsed = ParseGuardianScope(Name(scope));

        ASSERT_TRUE(parsed.has_value()) << Name(scope);
        EXPECT_EQ(*parsed, scope);
    }
    EXPECT_FALSE(ParseGuardianScope("everything").has_value())
        << "единого «родитель видит всё» не бывает даже опечаткой";
}

TEST(MaturityRuleTest, ZeroWindowIsAnInstantCutOff) {
    const auto refused = MaturityRule::Compose(AgeThresholds::Compose(14, 16, 18).Value(),
                                               core::Instant::Duration::zero());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "maturity_grace_not_positive");
}

TEST(Maturity, TurningYearsLandsOnTheBirthday) {
    const auto born = BirthDate::Of(2011, 3, 4).Value();
    const auto grown = AgeStatus::TurnsAt(born, 14);

    EXPECT_EQ(AgeStatus::At(born, grown).Value().Years(), 14);
    EXPECT_EQ(AgeStatus::At(born, grown - Days(1)).Value().Years(), 13);
}

/// Двадцать девятое февраля: в невисокосный год такого дня нет, и
/// совершеннолетие наступает первого марта, а не исчезает.
TEST(Maturity, TheTwentyNinthOfFebruaryBecomesTheFirstOfMarch) {
    const auto born = BirthDate::Of(2012, 2, 29).Value();
    const auto grown = AgeStatus::TurnsAt(born, 15);

    EXPECT_EQ(AgeStatus::At(born, grown).Value().Years(), 15);
    EXPECT_EQ(AgeStatus::At(born, grown - Days(1)).Value().Years(), 14);
}

class WeighingTest : public ::testing::Test {
protected:
    /// Ученику четырнадцать исполняется ровно в этот момент.
    core::Instant GrownAt() const {
        return AgeStatus::TurnsAt(born_, 14);
    }

    GuardianAccess Weigh(const std::vector<GuardianConsent>& consents,
                         core::Instant now,
                         bool guardianship_holds = true) const {
        return WeighConsents(consents, born_, Rule(), now, guardianship_holds);
    }

    BirthDate born_{BirthDate::Of(2011, 3, 4).Value()};
};

TEST_F(WeighingTest, WhatWasGrantedIsOpenWhileTheStudentIsSmall) {
    const auto before = GrownAt() - Days(400);
    const auto access = Weigh({Consent(GuardianScope::kSchedule, kTutor, before),
                               Consent(GuardianScope::kRecordings, kTutor, before, 2)},
                              before + Days(1));

    EXPECT_TRUE(access.Open().Has(GuardianScope::kSchedule));
    EXPECT_TRUE(access.Open().Has(GuardianScope::kRecordings));
    EXPECT_TRUE(access.Deciding().Empty());
    EXPECT_TRUE(access.AwaitsStudent().Empty());
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: переход через совершеннолетие подменяемыми
/// часами. Три момента, три разных ответа — и ни одного мгновенного обрыва.
TEST_F(WeighingTest, TheDoorClosesAfterTheWindow) {
    const auto granted = GrownAt() - Days(400);
    const std::vector<GuardianConsent> consents{
        Consent(GuardianScope::kSchedule, kTutor, granted),
        Consent(GuardianScope::kRecordings, kTutor, granted, 2)};

    const auto before = Weigh(consents, GrownAt() - Days(1));
    EXPECT_TRUE(before.Open().Has(GuardianScope::kRecordings));
    EXPECT_TRUE(before.Deciding().Empty());

    const auto inside = Weigh(consents, GrownAt() + Days(29));
    EXPECT_TRUE(inside.Open().Has(GuardianScope::kRecordings))
        << "доступ оборвался в день рождения, посреди учебного года";
    EXPECT_TRUE(inside.Deciding().Has(GuardianScope::kRecordings))
        << "срок на решение пошёл, а сказать об этом нечем";
    EXPECT_TRUE(inside.AwaitsStudent().Empty());

    const auto after = Weigh(consents, GrownAt() + Days(31));
    EXPECT_FALSE(after.Open().Has(GuardianScope::kRecordings));
    EXPECT_TRUE(after.AwaitsStudent().Has(GuardianScope::kRecordings));

    EXPECT_TRUE(after.Open().Has(GuardianScope::kSchedule))
        << "расписание оборвалось на первом пороге: за занятия платит родитель";
}

/// А вот на совершеннолетии кончается и расписание: «до 18» означает и «не
/// после».
TEST_F(WeighingTest, AtMajorityEvenTheScheduleGoesToTheStudent) {
    const auto granted = GrownAt() - Days(400);
    const std::vector<GuardianConsent> consents{Consent(GuardianScope::kSchedule, kTutor, granted)};
    const auto adult = AgeStatus::TurnsAt(born_, 18);

    EXPECT_TRUE(Weigh(consents, adult - Days(1)).Open().Has(GuardianScope::kSchedule));
    EXPECT_TRUE(Weigh(consents, adult + Days(1)).Deciding().Has(GuardianScope::kSchedule));
    EXPECT_TRUE(Weigh(consents, adult + Days(31)).AwaitsStudent().Has(GuardianScope::kSchedule))
        << "у восемнадцатилетнего расписанием по-прежнему распоряжается родитель";
}

/// Слово самого ученика снимает вопрос навсегда: подтверждённое им согласие
/// совершеннолетие не трогает.
TEST_F(WeighingTest, WhatTheStudentGrantedHimselfSurvives) {
    const auto granted = GrownAt() - Days(10);
    const auto access =
        Weigh({Consent(GuardianScope::kRecordings, kStudent, granted)}, GrownAt() + Days(365));

    EXPECT_TRUE(access.Open().Has(GuardianScope::kRecordings));
    EXPECT_TRUE(access.AwaitsStudent().Empty());
}

TEST_F(WeighingTest, RevokedAndExpiredConsentsOpenNothing) {
    const auto granted = GrownAt() - Days(400);
    const auto revoked =
        Consent(GuardianScope::kSchedule, kTutor, granted).Revoked(granted + Days(1), kStudent);
    ASSERT_TRUE(revoked.HasValue());

    const auto access = Weigh({revoked.Value()}, granted + Days(2));
    EXPECT_TRUE(access.Open().Empty());

    const auto expiring = GuardianConsent::Grant(Numbered<ConsentId>(9),
                                                 kTenant,
                                                 kGuardian,
                                                 kStudent,
                                                 GuardianScope::kPayments,
                                                 ConsentBasis::kGuardianship,
                                                 kTutor,
                                                 granted,
                                                 granted + Days(5))
                              .Value();
    EXPECT_TRUE(Weigh({expiring}, granted + Days(4)).Open().Has(GuardianScope::kPayments));
    EXPECT_TRUE(Weigh({expiring}, granted + Days(6)).Open().Empty());
}

/// Дата рождения неизвестна — правило совершеннолетия не срабатывает. Отобрать
/// доступ у родителя из-за пустой колонки хуже, чем оставить его до выяснения.
TEST_F(WeighingTest, AnUnknownBirthDateDoesNotCloseAnything) {
    const auto granted = core::Instant::FromUnixMicros(0);
    const std::vector<GuardianConsent> consents{
        Consent(GuardianScope::kRecordings, kTutor, granted)};

    const auto access = WeighConsents(consents, std::nullopt, Rule(), granted + Days(10000), true);

    EXPECT_TRUE(access.Open().Has(GuardianScope::kRecordings));
    EXPECT_TRUE(access.AwaitsStudent().Empty());
}

TEST(GuardianConsentTest, RevokingIsADateAndTheRowStays) {
    const auto at = core::Instant::FromUnixMicros(0);
    const auto consent = Consent(GuardianScope::kPayments, kTutor, at);

    const auto revoked = consent.Revoked(at + Days(1), kStudent);
    ASSERT_TRUE(revoked.HasValue());
    EXPECT_EQ(revoked.Value().RevokedAt(), at + Days(1));
    EXPECT_EQ(revoked.Value().RevokedBy(), kStudent);
    EXPECT_EQ(revoked.Value().GrantedAt(), consent.GrantedAt())
        << "отзыв переписал выдачу: «кто имел доступ в марте» больше не ответить";
    EXPECT_FALSE(revoked.Value().IsActiveAt(at + Days(2)));

    const auto again = revoked.Value().Revoked(at + Days(2), kStudent);
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "consent_already_revoked");
}

TEST(GuardianConsentTest, SelfGuardianshipIsRefused) {
    const auto refused = GuardianConsent::Grant(Numbered<ConsentId>(1),
                                                kTenant,
                                                kStudent,
                                                kStudent,
                                                GuardianScope::kSchedule,
                                                ConsentBasis::kGuardianship,
                                                kTutor,
                                                core::Instant::FromUnixMicros(0),
                                                std::nullopt);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "consent_self_guardianship");
}

/// СКВОЗНАЯ ПРОВЕРКА ДВЕРИ И ЖУРНАЛА. Тот же вызов, который решает права,
/// оставляет след, — и обойти его нечем, потому что обойти проверку прав нечем.
class GuardianDoorTest : public ::testing::Test {
protected:
    GuardianDoorTest() {
        world_.roles.Grant(kTenant, kTutor, Role::kTutor);
        world_.roles.Grant(kTenant, kStudent, Role::kStudent);
        world_.roles.Grant(kTenant, kGuardian, Role::kGuardian);
        world_.guardianships.Establish(kTenant, kGuardian, kStudent);
    }

    Resource Lesson() const {
        return Resource{kTenant, kTutor, kStudent};
    }

    core::Result<std::vector<AccessRecord>> JournalFor(const core::PersonId& actor) const {
        const ShowAccessJournal journal{world_.contract, world_.journal};
        return journal.Execute(kTenant, actor, Lesson(), core::Instant::FromUnixMicros(0));
    }

    testing::AccessWorld world_;
};

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: опекун без включённого уровня получает отказ —
/// И ЭТОТ ОТКАЗ ПОПАДАЕТ В ЖУРНАЛ. «Кто-то пытался открыть твою запись занятия,
/// и ему не дали» — сведение, которое ученику нужнее списка удачных просмотров.
TEST_F(GuardianDoorTest, ARefusedGuardianLeavesTheRefusalInTheJournal) {
    const auto decision =
        world_.contract.Decide(kTenant, kGuardian, Action::kViewLessonRecording, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kScopeMissing);

    ASSERT_EQ(world_.journal.Rows().size(), 1U) << "отказ прошёл мимо журнала";
    const auto& row = world_.journal.Rows().front();
    EXPECT_EQ(row.Actor(), kGuardian);
    EXPECT_EQ(row.Subject(), kStudent);
    EXPECT_EQ(row.Kind(), ResourceKind::kRecording);
    EXPECT_EQ(row.Outcome(), AccessOutcome::kRefused);
    EXPECT_EQ(row.At(), world_.clock.Now());
}

TEST_F(GuardianDoorTest, AnOpenedLevelIsShownAndStillLeavesATrace) {
    world_.Open(kTenant, kGuardian, kStudent, GuardianScope::kRecordings, kStudent);

    const auto decision =
        world_.contract.Decide(kTenant, kGuardian, Action::kViewLessonRecording, Lesson());

    EXPECT_TRUE(decision.allowed);
    ASSERT_EQ(world_.journal.Rows().size(), 1U)
        << "право смотреть засчитали за право смотреть незаметно";
    EXPECT_EQ(world_.journal.Rows().front().Outcome(), AccessOutcome::kShown);
}

/// Расписание журнала не касается: журнал обо всём — журнал, в который никто не
/// смотрит, и настоящие заходы в нём тонут.
TEST_F(GuardianDoorTest, OpeningTheScheduleIsNotJournalled) {
    world_.Open(kTenant, kGuardian, kStudent, GuardianScope::kSchedule, kTutor);

    EXPECT_TRUE(
        world_.contract.Decide(kTenant, kGuardian, Action::kViewSchedule, Lesson()).allowed);
    EXPECT_TRUE(world_.journal.Rows().empty());
}

TEST_F(GuardianDoorTest, ReadingOnesOwnRecordingIsNotJournalled) {
    EXPECT_TRUE(
        world_.contract.Decide(kTenant, kStudent, Action::kViewLessonRecording, Lesson()).allowed);
    EXPECT_TRUE(world_.journal.Rows().empty()) << "незаметно смотрят чужое, а не собственное";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: ученик видит журнал доступов к себе. Это и есть
/// настоящая гарантия родительского доступа, а не абзац в политике.
TEST_F(GuardianDoorTest, TheStudentSeesWhoCameForHisRecordings) {
    world_.Open(kTenant, kGuardian, kStudent, GuardianScope::kRecordings, kStudent);
    static_cast<void>(
        world_.contract.Decide(kTenant, kGuardian, Action::kViewLessonRecording, Lesson()));
    world_.clock.Advance(Days(1));
    static_cast<void>(
        world_.contract.Decide(kTenant, kGuardian, Action::kViewLessonTranscript, Lesson()));

    const auto seen = JournalFor(kStudent);

    ASSERT_TRUE(seen.HasValue()) << seen.Failure().Code();
    ASSERT_EQ(seen.Value().size(), 2U);
    EXPECT_EQ(seen.Value().front().Actor(), kGuardian);
    EXPECT_EQ(seen.Value().front().Kind(), ResourceKind::kRecording);
    EXPECT_EQ(seen.Value().back().Kind(), ResourceKind::kTranscript);
}

/// Опекун журнала не видит ни при каком наборе уровней: иначе он знает, заметил
/// ли ребёнок его просмотры, и гарантия перестаёт быть гарантией.
TEST_F(GuardianDoorTest, TheGuardianDoesNotGetTheJournalEvenWithEveryLevel) {
    for (const auto scope : kEveryGuardianScope) {
        world_.Open(kTenant, kGuardian, kStudent, scope, kStudent);
    }

    const auto refused = JournalFor(kGuardian);

    ASSERT_FALSE(refused.HasValue()) << "опекун читает журнал доступа к ребёнку";
    EXPECT_EQ(refused.Failure().Code(), "journal_not_yours");
    EXPECT_NE(refused.Failure().Detail().find(Name(DenyReason::kRoleMissing)), std::string::npos)
        << "причина отказа потерялась по дороге: разбирать жалобу будет нечем";
}

TEST_F(GuardianDoorTest, TheTutorSeesTheJournalOfHisOwnStudent) {
    EXPECT_TRUE(JournalFor(kTutor).HasValue());
}

TEST_F(GuardianDoorTest, AJournalAboutNobodyIsRefusedBeforeAnyRights) {
    const ShowAccessJournal journal{world_.contract, world_.journal};

    const auto refused = journal.Execute(kTenant,
                                         kStudent,
                                         Resource{kTenant, std::nullopt, std::nullopt},
                                         core::Instant::FromUnixMicros(0));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "journal_without_subject");
}

/// Выдача, отзыв и подтверждение — на настоящих сценариях, а не на фейке
/// согласий: правила выдачи живут в сценарии, и проверять их надо там.
class GrantingTest : public ::testing::Test {
protected:
    GrantingTest() {
        world_.guardianships.Establish(kTenant, kGuardian, kStudent);
        world_.birth_dates.Put(kTenant, kStudent, born_);
    }

    core::Result<GuardianConsent> Ask(GuardianScope scope,
                                      core::PersonId granted_by,
                                      core::PersonId guardian = kGuardian) {
        const GrantGuardianScope grant{world_.guardianships,
                                       world_.consents,
                                       world_.birth_dates,
                                       world_.maturity,
                                       world_.ids,
                                       world_.clock};
        return grant.Execute(GrantGuardianScopeRequest{kTenant,
                                                       std::move(guardian),
                                                       kStudent,
                                                       scope,
                                                       ConsentBasis::kGuardianship,
                                                       std::move(granted_by),
                                                       std::nullopt});
    }

    core::Result<void> Close(GuardianScope scope, core::PersonId revoked_by) {
        const RevokeGuardianScope revoke{world_.consents, world_.clock};
        return revoke.Execute(
            RevokeGuardianScopeRequest{kTenant, kGuardian, kStudent, scope, std::move(revoked_by)});
    }

    void GrowUp() {
        world_.clock.SetNow(AgeStatus::TurnsAt(born_, 14) + Days(1));
    }

    testing::AccessWorld world_;
    BirthDate born_{BirthDate::Of(2011, 3, 4).Value()};
};

TEST_F(GrantingTest, ALevelWithoutGuardianshipIsRefused) {
    const auto refused = Ask(GuardianScope::kSchedule, kTutor, Numbered<core::PersonId>(31));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "guardianship_not_found");
}

TEST_F(GrantingTest, TheSameLevelIsNotGrantedTwice) {
    ASSERT_TRUE(Ask(GuardianScope::kSchedule, kTutor).HasValue());

    const auto again = Ask(GuardianScope::kSchedule, kTutor);
    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Code(), "consent_already_granted");
}

/// ПОСЛЕ СОВЕРШЕННОЛЕТИЯ ЧУВСТВИТЕЛЬНЫЙ УРОВЕНЬ ОТКРЫВАЕТ ТОЛЬКО САМ УЧЕНИК.
/// Ни репетитор, ни родитель — иначе окно на решение обходится в один запрос.
TEST_F(GrantingTest, AfterMajorityOnlyTheStudentOpensTheSensitiveLevel) {
    GrowUp();

    const auto refused = Ask(GuardianScope::kRecordings, kTutor);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "consent_needs_student_word");

    const auto granted = Ask(GuardianScope::kRecordings, kStudent);
    ASSERT_TRUE(granted.HasValue()) << granted.Failure().Code();
    EXPECT_TRUE(granted.Value().GrantedByStudent());
}

/// Расписание и деньги совершеннолетия не спрашивают: за занятия платит родитель,
/// и обрывать ему счета в день рождения не за что.
TEST_F(GrantingTest, MoneyAndScheduleDoNotWaitForTheStudentWord) {
    GrowUp();

    EXPECT_TRUE(Ask(GuardianScope::kSchedule, kTutor).HasValue());
    EXPECT_TRUE(Ask(GuardianScope::kPayments, kTutor).HasValue());
}

/// ОТЗЫВ — ЭТО ДАТА, А НЕ УДАЛЕНИЕ СТРОКИ.
TEST_F(GrantingTest, RevokingClosesTheLevelAndKeepsTheRow) {
    ASSERT_TRUE(Ask(GuardianScope::kPayments, kTutor).HasValue());
    ASSERT_EQ(world_.consents.Rows().size(), 1U);
    world_.clock.Advance(Days(3));

    ASSERT_TRUE(Close(GuardianScope::kPayments, kStudent).HasValue());

    EXPECT_EQ(world_.consents.Rows().size(), 1U)
        << "строку удалили: на вопрос «кто имел доступ в марте» больше не ответить";
    EXPECT_TRUE(world_.consents.Rows().front().RevokedAt().has_value());
    EXPECT_TRUE(world_.consents.ActiveFor(kTenant, kGuardian, kStudent).empty());

    const auto again = Close(GuardianScope::kPayments, kStudent);
    ASSERT_FALSE(again.HasValue()) << "повторный отзыв прошёл как новый";
    EXPECT_EQ(again.Failure().Code(), "consent_not_found");
}

TEST_F(GrantingTest, WhatWasRevokedIsGrantedAgainAsANewRow) {
    ASSERT_TRUE(Ask(GuardianScope::kSchedule, kTutor).HasValue());
    ASSERT_TRUE(Close(GuardianScope::kSchedule, kStudent).HasValue());

    ASSERT_TRUE(Ask(GuardianScope::kSchedule, kTutor).HasValue());
    EXPECT_EQ(world_.consents.Rows().size(), 2U);
}

/// Уведомление обеим сторонам: не побочный эффект проверки прав, а сценарий,
/// который молчит, когда объявлять нечего.
class HandoverTest : public ::testing::Test {
protected:
    HandoverTest() {
        world_.guardianships.Establish(kTenant, kGuardian, kStudent);
        world_.birth_dates.Put(kTenant, kStudent, born_);
        world_.Open(kTenant, kGuardian, kStudent, GuardianScope::kRecordings, kTutor);
        bus_.Subscribe<pdr::events::identity::GuardianHandoverStarted>(
            [this](const pdr::events::identity::GuardianHandoverStarted& event) {
                heard_.push_back(event);
            });
    }

    bool Announce() {
        const AnnounceGuardianHandover announce{world_.consents,
                                                world_.guardianships,
                                                world_.birth_dates,
                                                world_.maturity,
                                                world_.clock,
                                                bus_};
        const auto said = announce.Execute(kTenant, kGuardian, kStudent);
        EXPECT_TRUE(said.HasValue());
        return said.HasValue() && said.Value();
    }

    core::Instant GrownAt() const {
        return AgeStatus::TurnsAt(born_, 14);
    }

    testing::AccessWorld world_;
    pdr::events::InMemoryBus bus_;
    std::vector<pdr::events::identity::GuardianHandoverStarted> heard_;
    BirthDate born_{BirthDate::Of(2011, 3, 4).Value()};
};

TEST_F(HandoverTest, NothingIsAnnouncedWhileTheStudentIsSmall) {
    world_.clock.SetNow(GrownAt() - Days(1));

    EXPECT_FALSE(Announce());
    EXPECT_TRUE(heard_.empty()) << "событие без повода: на такие перестают смотреть";
}

TEST_F(HandoverTest, TheWindowIsAnnouncedWithItsDeadline) {
    world_.clock.SetNow(GrownAt() + Days(1));

    EXPECT_TRUE(Announce());
    ASSERT_EQ(heard_.size(), 1U);
    EXPECT_EQ(heard_.front().guardian, kGuardian);
    EXPECT_EQ(heard_.front().student, kStudent);
    EXPECT_EQ(heard_.front().decide_by, GrownAt() + Days(30))
        << "срок на решение назвали не тот, что закрывает доступ";
}

TEST_F(HandoverTest, AfterTheWindowThereIsNothingToAnnounce) {
    world_.clock.SetNow(GrownAt() + Days(31));

    EXPECT_FALSE(Announce()) << "объявили срок, который уже кончился";
}

TEST_F(HandoverTest, AnUnknownBirthDateIsSilent) {
    testing::AccessWorld blank;
    blank.Open(kTenant, kGuardian, kStudent, GuardianScope::kRecordings, kTutor);
    const AnnounceGuardianHandover announce{
        blank.consents, blank.guardianships, blank.birth_dates, blank.maturity, blank.clock, bus_};

    const auto said = announce.Execute(kTenant, kGuardian, kStudent);

    ASSERT_TRUE(said.HasValue());
    EXPECT_FALSE(said.Value());
}

}  // namespace
}  // namespace pdr::identity
