#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "events/in_memory_bus.hpp"
#include "fakes/fake_id_generator.hpp"
#include "identity/application/announce_coming_of_age.hpp"
#include "identity/application/grant_guardian_scope.hpp"
#include "identity/application/notify_guardian_of_act.hpp"
#include "identity/application/revoke_guardian_scope.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/capabilities.hpp"
#include "identity/core/guardian_consent.hpp"
#include "identity/core/independent_act.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

core::Instant::Duration Days(int days) {
    return std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * days});
}

const auto kTenant = Numbered<core::TenantId>(1);
const auto kTutor = Numbered<core::PersonId>(10);
const auto kAdult = Numbered<core::PersonId>(20);
const auto kSpouse = Numbered<core::PersonId>(30);
const auto kEmployer = Numbered<core::PersonId>(40);

/// Взрослый ученик: языки, вуз, переподготовка. Опекуна у него НЕТ ВОВСЕ —
/// именно поэтому фейк опеки в этом мире остаётся пустым от начала и до конца.
const BirthDate kBornOn = BirthDate::Of(1990, 5, 20).Value();

class AdultStudentTest : public ::testing::Test {
protected:
    AdultStudentTest() {
        world_.roles.Grant(kTenant, kTutor, Role::kTutor);
        world_.roles.Grant(kTenant, kAdult, Role::kStudent);
        world_.roles.Grant(kTenant, kSpouse, Role::kGuardian);
        world_.roles.Grant(kTenant, kEmployer, Role::kGuardian);
        world_.birth_dates.Put(kTenant, kAdult, kBornOn);
        world_.clock.SetNow(AgeStatus::TurnsAt(kBornOn, 34));
    }

    Resource Lesson() const {
        return Resource{kTenant, kTutor, kAdult};
    }

    PolicyDecision AsAdult(Action action) const {
        return world_.contract.Decide(kTenant, kAdult, action, Lesson());
    }

    PolicyDecision As(const core::PersonId& who, Action action) const {
        return world_.contract.Decide(kTenant, who, action, Lesson());
    }

    GrantGuardianScope Granting() {
        return GrantGuardianScope{world_.guardianships,
                                  world_.consents,
                                  world_.birth_dates,
                                  world_.maturity,
                                  ids_,
                                  world_.clock};
    }

    core::Result<GuardianConsent> Name(const core::PersonId& watcher,
                                       GuardianScope scope,
                                       ConsentBasis basis,
                                       const core::PersonId& by) {
        return Granting().Execute(
            GrantGuardianScopeRequest{kTenant, watcher, kAdult, scope, basis, by, std::nullopt});
    }

    testing::AccessWorld world_;
    pdr::testing::FakeIdGenerator ids_;
};

/// ГЛАВНЫЙ ТЕСТ ЗАДАЧИ: полный путь взрослого ученика БЕЗ ЕДИНОГО ОПЕКУНА.
///
/// Перебор по всему реестру действий: всё, что ученику вообще даётся, взрослый
/// получает сам — не заводя опеки, не спрашивая её и не упираясь в её
/// отсутствие. Это тот класс ошибок, который вылезает на демонстрации: модель,
/// в которой ученик без опекуна выглядит потерявшимся.
TEST_F(AdultStudentTest, EveryStudentActionWorksWithoutASingleGuardian) {
    ASSERT_TRUE(world_.guardianships.GuardiansOf(kTenant, kAdult).empty())
        << "мир проверки завёл опеку: она бы и объясняла успех";

    int checked = 0;
    for (const auto action : kEveryAction) {
        const Subject student{
            kTenant,
            kAdult,
            RoleSet{}.With(Role::kStudent),
            Tie::kAboutMe,
            GuardianAccess{GuardianScopeSet{}, GuardianScopeSet{}, GuardianScopeSet{}},
            Capabilities::Everything()};
        if (!world_.permissions.Decide(student, action, Lesson()).allowed) {
            continue;
        }
        ++checked;

        EXPECT_TRUE(AsAdult(action).allowed)
            << "взрослому ученику отказали в «" << pdr::identity::Name(action)
            << "»: опеки у него нет и не будет";
    }

    EXPECT_GT(checked, 0) << "ученику не даётся ничего: перебор ничего не доказывает";
    EXPECT_TRUE(world_.guardianships.GuardiansOf(kTenant, kAdult).empty())
        << "по дороге завелась опека";
}

TEST_F(AdultStudentTest, HeHasEveryCapabilityAndNoGuardian) {
    const auto able = Compute(AgeStatus::At(kBornOn, world_.clock.Now()).Value(),
                              world_.maturity.Rule().Value().Thresholds());

    EXPECT_EQ(able, Capabilities::Everything())
        << "у взрослого не хватает возможности, которую ему неоткуда взять";
    EXPECT_TRUE(world_.guardianships.GuardiansOf(kTenant, kAdult).empty());
}

/// Уведомлять о самостоятельном поступке некому — и это не ошибка.
TEST_F(AdultStudentTest, NobodyIsNotifiedAboutHisOwnLessons) {
    pdr::events::InMemoryBus bus;
    const NotifyGuardianOfAct notify{world_.guardianships, world_.clock, bus};

    for (const auto act : kEveryIndependentAct) {
        const auto told = notify.Execute(NotifyGuardianOfActRequest{kTenant, kAdult, act});
        ASSERT_TRUE(told.HasValue());
        EXPECT_EQ(told.Value(), 0) << pdr::identity::Name(act);
    }
    EXPECT_EQ(bus.Published(), 0U) << "письмо ушло опекуну, которого нет";
}

TEST_F(AdultStudentTest, ComingOfAgeIsNotAnnouncedToNobody) {
    pdr::events::InMemoryBus bus;
    const AnnounceComingOfAge announce{
        world_.guardianships, world_.birth_dates, world_.maturity, world_.clock, bus};

    world_.clock.SetNow(AgeStatus::TurnsAt(kBornOn, 18));
    const auto said = announce.Execute(kTenant, kAdult);

    ASSERT_TRUE(said.HasValue()) << said.Failure().Code();
    EXPECT_EQ(said.Value(), 1) << "взрослому о его же совершеннолетии не сказали";
    EXPECT_EQ(bus.Published(), 1U);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: взрослый сам добавляет наблюдателя и сам его
/// отзывает. Опеки при этом не заводится ни на секунду.
TEST_F(AdultStudentTest, HeNamesAWatcherHimselfAndTakesItBack) {
    const auto named =
        Name(kSpouse, GuardianScope::kSchedule, ConsentBasis::kNamedByStudent, kAdult);
    ASSERT_TRUE(named.HasValue()) << named.Failure().Code();
    EXPECT_EQ(named.Value().Basis(), ConsentBasis::kNamedByStudent);
    EXPECT_FALSE(named.Value().RestsOnGuardianship());
    EXPECT_TRUE(world_.guardianships.GuardiansOf(kTenant, kAdult).empty())
        << "чтобы назвать супруга, пришлось завести опеку над взрослым человеком";

    EXPECT_TRUE(As(kSpouse, Action::kViewSchedule).allowed)
        << "названный наблюдатель не видит того, ради чего его назвали";

    const RevokeGuardianScope revoke{world_.consents, world_.clock};
    const auto closed = revoke.Execute(
        RevokeGuardianScopeRequest{kTenant, kSpouse, kAdult, GuardianScope::kSchedule, kAdult});
    ASSERT_TRUE(closed.HasValue()) << closed.Failure().Code();

    EXPECT_FALSE(As(kSpouse, Action::kViewSchedule).allowed) << "отзыв не сработал";
    EXPECT_EQ(world_.consents.Rows().size(), 1U)
        << "отзыв удалил строку: на вопрос «кто смотрел в марте» больше не ответить";
}

TEST_F(AdultStudentTest, AWatcherIsNamedByTheOneWatched) {
    const auto refused =
        Name(kSpouse, GuardianScope::kSchedule, ConsentBasis::kNamedByStudent, kTutor);

    ASSERT_FALSE(refused.HasValue()) << "репетитор назначил взрослому наблюдателя";
    EXPECT_EQ(refused.Failure().Code(), "watcher_named_by_someone_else");
}

/// Наблюдатель взрослого получает те же уровни, что и опекун ребёнка: механизм
/// один, и записи занятий так же не открываются сами.
TEST_F(AdultStudentTest, AWatcherGetsTheSameLevelsAsAGuardian) {
    ASSERT_TRUE(Name(kSpouse, GuardianScope::kRecordings, ConsentBasis::kNamedByStudent, kAdult)
                    .HasValue());

    EXPECT_TRUE(As(kSpouse, Action::kViewLessonRecording).allowed);
    EXPECT_FALSE(As(kSpouse, Action::kViewSchedule).allowed) << "один уровень открыл соседний";
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: плательщик без выданного доступа не видит ни
/// аналитики, ни конспектов. ДЕНЬГИ НЕ ДАЮТ ПРАВА СМОТРЕТЬ.
TEST_F(AdultStudentTest, ThePayerSeesTheBillAndNothingElse) {
    const auto pays =
        Name(kEmployer, GuardianScope::kPayments, ConsentBasis::kPaysForLessons, kAdult);
    ASSERT_TRUE(pays.HasValue()) << pays.Failure().Code();

    EXPECT_TRUE(As(kEmployer, Action::kViewInvoice).allowed) << "плательщик не видит счёта";
    EXPECT_TRUE(As(kEmployer, Action::kPayInvoice).allowed);

    for (const auto action : {Action::kViewProgress,
                              Action::kExportProgress,
                              Action::kViewMaterial,
                              Action::kViewLessonRecording,
                              Action::kViewLessonTranscript,
                              Action::kViewAccessJournal}) {
        const auto decision = As(kEmployer, action);
        EXPECT_FALSE(decision.allowed)
            << "работодатель смотрит «" << pdr::identity::Name(action) << "», потому что платит";
    }
}

/// И выдать плательщику что-то сверх денег НЕЧЕМ: отказ приходит из домена, а
/// не из забытой проверки в интерфейсе.
TEST_F(AdultStudentTest, MoneyCannotBeTurnedIntoSight) {
    for (const auto scope : kEveryGuardianScope) {
        const auto asked = Name(kEmployer, scope, ConsentBasis::kPaysForLessons, kAdult);

        if (scope == GuardianScope::kPayments) {
            EXPECT_TRUE(asked.HasValue()) << pdr::identity::Name(scope);
            continue;
        }

        ASSERT_FALSE(asked.HasValue()) << pdr::identity::Name(scope);
        EXPECT_EQ(asked.Failure().Code(), "consent_basis_forbids_scope");
    }
}

TEST_F(AdultStudentTest, AWatcherNeedsNoGuardianshipButAGuardianDoes) {
    const auto without =
        Name(kSpouse, GuardianScope::kSchedule, ConsentBasis::kGuardianship, kTutor);

    ASSERT_FALSE(without.HasValue()) << "опекунский доступ выдали без опеки";
    EXPECT_EQ(without.Failure().Code(), "guardianship_not_found");

    EXPECT_TRUE(
        Name(kSpouse, GuardianScope::kSchedule, ConsentBasis::kNamedByStudent, kAdult).HasValue())
        << "наблюдатель взрослого упёрся в отсутствие опеки";
}

/// Отозванная опека обрывает опекунский доступ, сколько бы строк ни осталось, —
/// и не трогает того, кого взрослый назвал сам.
TEST(ConsentBasisTest, GuardianshipBackedAccessDiesWithTheGuardianship) {
    testing::AccessWorld world;
    const auto student = Numbered<core::PersonId>(21);
    const auto parent = Numbered<core::PersonId>(31);
    const auto coach = Numbered<core::PersonId>(41);
    world.roles.Grant(kTenant, student, Role::kStudent);
    world.roles.Grant(kTenant, parent, Role::kGuardian);
    world.roles.Grant(kTenant, coach, Role::kGuardian);
    world.birth_dates.Put(kTenant, student, BirthDate::Of(1990, 5, 20).Value());
    world.clock.SetNow(AgeStatus::TurnsAt(BirthDate::Of(1990, 5, 20).Value(), 34));

    world.guardianships.Establish(kTenant, parent, student);
    world.Open(kTenant, parent, student, GuardianScope::kSchedule, student);
    world.Open(
        kTenant, coach, student, GuardianScope::kSchedule, student, ConsentBasis::kNamedByStudent);

    const Resource lesson{kTenant, kTutor, student};
    ASSERT_TRUE(world.contract.Decide(kTenant, parent, Action::kViewSchedule, lesson).allowed);
    ASSERT_TRUE(world.contract.Decide(kTenant, coach, Action::kViewSchedule, lesson).allowed);

    const auto link = world.guardianships.FindActive(kTenant, parent, student);
    ASSERT_TRUE(link.has_value());
    world.guardianships.Save(link->Revoked(world.clock.Now()).Value());

    EXPECT_FALSE(world.contract.Decide(kTenant, parent, Action::kViewSchedule, lesson).allowed)
        << "опеку отозвали, а доступ остался строкой согласия";
    EXPECT_TRUE(world.contract.Decide(kTenant, coach, Action::kViewSchedule, lesson).allowed)
        << "чужой отзыв опеки закрыл доступ тому, кого взрослый назвал сам";
}

TEST(ConsentBasisTest, CodesAreTheWordsTheDatabaseKnows) {
    for (const auto basis : kEveryConsentBasis) {
        const auto parsed = ParseConsentBasis(pdr::identity::Name(basis));

        ASSERT_TRUE(parsed.has_value()) << pdr::identity::Name(basis);
        EXPECT_EQ(*parsed, basis);
    }
    EXPECT_FALSE(ParseConsentBasis("because_i_said_so").has_value());
}

TEST(ConsentBasisTest, MoneyCarriesMoneyAndNothingElse) {
    for (const auto scope : kEveryGuardianScope) {
        EXPECT_TRUE(MayCarry(ConsentBasis::kGuardianship, scope));
        EXPECT_TRUE(MayCarry(ConsentBasis::kNamedByStudent, scope));
        EXPECT_EQ(MayCarry(ConsentBasis::kPaysForLessons, scope), scope == GuardianScope::kPayments)
            << pdr::identity::Name(scope);
    }
}

}  // namespace
}  // namespace pdr::identity
