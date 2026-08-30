#include "identity/core/capabilities.hpp"

#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "events/identity/capabilities_widened.hpp"
#include "events/identity/ward_acted_alone.hpp"
#include "events/in_memory_bus.hpp"
#include "identity/application/announce_coming_of_age.hpp"
#include "identity/application/notify_guardian_of_act.hpp"
#include "identity/core/age_status.hpp"
#include "identity/core/independent_act.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

core::Instant::Duration Days(int days) {
    return std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * days});
}

const auto kTenant = Numbered<core::TenantId>(1);
const auto kTutor = Numbered<core::PersonId>(10);
const auto kStudent = Numbered<core::PersonId>(20);
const auto kGuardian = Numbered<core::PersonId>(30);
const auto kSecondGuardian = Numbered<core::PersonId>(31);

AgeThresholds Thresholds(int slots = 14, int payments = 16, int majority = 18) {
    return AgeThresholds::Compose(slots, payments, majority).Value();
}

const BirthDate kBornOn = BirthDate::Of(2011, 3, 4).Value();

Capabilities AbleAt(core::Instant moment, const AgeThresholds& thresholds = Thresholds()) {
    return Compute(AgeStatus::At(kBornOn, moment).Value(), thresholds);
}

TEST(AgeThresholdsTest, AbsurdAgesAreRefused) {
    EXPECT_FALSE(AgeThresholds::Compose(3, 16, 18).HasValue());
    EXPECT_FALSE(AgeThresholds::Compose(14, 16, 40).HasValue());
    EXPECT_EQ(AgeThresholds::Compose(3, 16, 18).Failure().Code(), "age_threshold_out_of_range");
}

/// Порядок порогов — не украшение: «платит раньше, чем двигает занятия» — это
/// набор, из которого получаются права, которых никто не задумывал.
TEST(AgeThresholdsTest, ThresholdsGoUpwards) {
    const auto refused = AgeThresholds::Compose(16, 14, 18);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "age_thresholds_out_of_order");
    EXPECT_TRUE(AgeThresholds::Compose(18, 18, 18).HasValue())
        << "все пороги на одном возрасте — это не поломка, а другая юрисдикция";
}

TEST(AgeThresholdsTest, CodesAreTheWordsTheRegistryKnows) {
    for (const auto threshold : kEveryAgeThreshold) {
        const auto parsed = ParseAgeThreshold(Name(threshold));

        ASSERT_TRUE(parsed.has_value()) << Name(threshold);
        EXPECT_EQ(*parsed, threshold);
    }
    for (const auto capability : kEveryCapability) {
        const auto parsed = ParseCapability(Name(capability));

        ASSERT_TRUE(parsed.has_value()) << Name(capability);
        EXPECT_EQ(*parsed, capability);
    }
    EXPECT_FALSE(ParseCapability("everything").has_value());
}

/// ГЛАВНЫЙ ТЕСТ ПОРОГОВ: день до, сам день, день после — по каждому.
///
/// Часы подменяемые, поэтому весь переход занимает микросекунды. Проверять
/// границу иначе нечем: ждать дня рождения тест не может, а посчитанный «в
/// голове» возраст проверял бы арифметику теста, а не домена.
TEST(CapabilitiesTest, EveryThresholdArrivesExactlyOnTheBirthday) {
    const auto thresholds = Thresholds();

    for (const auto capability : kEveryCapability) {
        const auto years = thresholds.Years(ArrivesWith(capability));
        const auto birthday = AgeStatus::TurnsAt(kBornOn, years);
        const std::string where{Name(capability)};

        EXPECT_FALSE(AbleAt(birthday - Days(1)).Has(capability)) << where << ": день до";
        EXPECT_TRUE(AbleAt(birthday).Has(capability)) << where << ": сам день";
        EXPECT_TRUE(AbleAt(birthday + Days(1)).Has(capability)) << where << ": день после";
    }
}

TEST(CapabilitiesTest, ALittleChildCanNothingHimself) {
    const auto small = AgeStatus::TurnsAt(kBornOn, 9);

    EXPECT_TRUE(AbleAt(small).Empty()) << "девятилетний распоряжается чем-то сам";
}

/// Пороги — значения конфига, а не константы: сдвинули число — сдвинулся день.
TEST(CapabilitiesTest, MovingTheThresholdMovesTheDay) {
    const auto later = Thresholds(16, 16, 18);
    const auto fourteen = AgeStatus::TurnsAt(kBornOn, 14);

    EXPECT_TRUE(AbleAt(fourteen).Has(Capability::kMoveOwnSlots));
    EXPECT_FALSE(AbleAt(fourteen, later).Has(Capability::kMoveOwnSlots))
        << "порог поменяли в конфиге, а права остались прежними";
}

TEST(CapabilitiesTest, MajorityAddsNoNewAbility) {
    const auto sixteen = AbleAt(AgeStatus::TurnsAt(kBornOn, 16));
    const auto eighteen = AbleAt(AgeStatus::TurnsAt(kBornOn, 18));

    EXPECT_EQ(sixteen, eighteen)
        << "к совершеннолетию прибавилась возможность, о которой не сказано";
    EXPECT_EQ(eighteen, Capabilities::Everything());
}

/// Мир, в котором ученику столько лет, сколько нужно тесту.
class GrowingUpTest : public ::testing::Test {
protected:
    GrowingUpTest() {
        world_.roles.Grant(kTenant, kTutor, Role::kTutor);
        world_.roles.Grant(kTenant, kStudent, Role::kStudent);
        world_.roles.Grant(kTenant, kGuardian, Role::kGuardian);
        world_.guardianships.Establish(kTenant, kGuardian, kStudent);
        world_.birth_dates.Put(kTenant, kStudent, kBornOn);

        Turns(7);
        for (const auto scope : kEveryGuardianScope) {
            world_.Open(kTenant, kGuardian, kStudent, scope, kTutor);
        }
        Turns(10);
    }

    void Turns(int years) {
        world_.clock.SetNow(AgeStatus::TurnsAt(kBornOn, years) + Days(1));
    }

    Resource Lesson() const {
        return Resource{kTenant, kTutor, kStudent};
    }

    PolicyDecision AsStudent(Action action) const {
        return world_.contract.Decide(kTenant, kStudent, action, Lesson());
    }

    PolicyDecision AsGuardian(Action action) const {
        return world_.contract.Decide(kTenant, kGuardian, action, Lesson());
    }

    testing::AccessWorld world_;
};

TEST_F(GrowingUpTest, BeforeTheFirstThresholdTheGuardianDoesEverything) {
    EXPECT_FALSE(AsStudent(Action::kCancelLesson).allowed);
    EXPECT_EQ(AsStudent(Action::kCancelLesson).reason, DenyReason::kTooYoung);
    EXPECT_TRUE(AsGuardian(Action::kCancelLesson).allowed);

    EXPECT_TRUE(AsStudent(Action::kViewSchedule).allowed)
        << "смотреть своё расписание человек вправе в любом возрасте";
}

TEST_F(GrowingUpTest, FromTheFirstThresholdHeMovesHisOwnSlots) {
    Turns(14);

    EXPECT_TRUE(AsStudent(Action::kCancelLesson).allowed);
    EXPECT_TRUE(AsStudent(Action::kRescheduleLesson).allowed);
    EXPECT_TRUE(AsStudent(Action::kWriteReview).allowed);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: ученик 14 не может оплатить.
TEST_F(GrowingUpTest, AtFourteenHeCannotPay) {
    Turns(14);

    const auto decision = AsStudent(Action::kPayInvoice);

    EXPECT_FALSE(decision.allowed) << "четырнадцатилетний платит сам";
    EXPECT_EQ(decision.reason, DenyReason::kTooYoung);
    EXPECT_TRUE(AsStudent(Action::kViewInvoice).allowed)
        << "видеть, сколько стоит его учёба, он вправе и раньше";
    EXPECT_TRUE(AsGuardian(Action::kPayInvoice).allowed);
}

TEST_F(GrowingUpTest, FromTheSecondThresholdHePaysAndChoosesHimself) {
    Turns(16);

    EXPECT_TRUE(AsStudent(Action::kPayInvoice).allowed);
    EXPECT_TRUE(AsStudent(Action::kBookLesson).allowed);
}

TEST_F(GrowingUpTest, AtFourteenHeStillDoesNotChooseTheTutor) {
    Turns(14);

    EXPECT_FALSE(AsStudent(Action::kBookLesson).allowed)
        << "выбор репетитора — второй порог, а не первый";
    EXPECT_TRUE(AsGuardian(Action::kBookLesson).allowed);
}

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: ученик 16 не может запустить автоплатёж с карты
/// опекуна. Деньги чужие, и «платит сам» этого не покрывает ни в каком возрасте.
TEST_F(GrowingUpTest, AtSixteenHeStillCannotChargeTheGuardiansCard) {
    Turns(16);
    ASSERT_TRUE(AsStudent(Action::kPayInvoice).allowed) << "своими средствами он уже платит";

    EXPECT_FALSE(AsStudent(Action::kManageAutoPayment).allowed)
        << "шестнадцатилетний подключил списание с родительской карты";
    EXPECT_TRUE(AsGuardian(Action::kManageAutoPayment).allowed)
        << "хозяин карты перестал ею распоряжаться";
}

TEST_F(GrowingUpTest, EvenAnAdultDoesNotTouchSomeoneElsesCard) {
    Turns(19);

    EXPECT_FALSE(AsStudent(Action::kManageAutoPayment).allowed)
        << "чужая карта стала своей от совершеннолетия";
}

/// ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ЗАДАЧИ: опекун сохраняет расписание при любом возрасте
/// до совершеннолетия.
TEST_F(GrowingUpTest, TheGuardianKeepsTheScheduleUntilMajority) {
    for (const auto years : {10, 14, 16, 17}) {
        Turns(years);
        EXPECT_TRUE(AsGuardian(Action::kViewSchedule).allowed) << "в " << years;
        EXPECT_TRUE(AsGuardian(Action::kCancelLesson).allowed) << "в " << years;
    }
}

TEST_F(GrowingUpTest, TheStudentGetsHisNotesBackAtTheFirstThreshold) {
    Turns(15);

    const auto decision = AsGuardian(Action::kViewLessonRecording);

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kStudentGrewUp);
}

TEST_F(GrowingUpTest, ASmallChildDoesNotRevokeHisGuardian) {
    EXPECT_FALSE(AsStudent(Action::kManageGuardianAccess).allowed)
        << "десятилетний закрыл родителю доступ";

    Turns(14);
    EXPECT_TRUE(AsStudent(Action::kManageGuardianAccess).allowed)
        << "уровни переходят к ученику, а распорядиться ими он не может";
}

/// Уведомления: опекун узнаёт о поступке подопечного всегда.
class TellingTheGuardianTest : public GrowingUpTest {
protected:
    TellingTheGuardianTest() {
        bus_.Subscribe<pdr::events::identity::WardActedAlone>(
            [this](const pdr::events::identity::WardActedAlone& event) {
                heard_.push_back(event);
            });
    }

    int Tell(IndependentAct act) {
        const NotifyGuardianOfAct notify{world_.guardianships, world_.clock, bus_};
        const auto told = notify.Execute(NotifyGuardianOfActRequest{kTenant, kStudent, act});
        EXPECT_TRUE(told.HasValue());
        return told.HasValue() ? told.Value() : 0;
    }

    pdr::events::InMemoryBus bus_;
    std::vector<pdr::events::identity::WardActedAlone> heard_;
};

/// ОБЯЗАТЕЛЬНЫЙ ТЕСТ ЗАДАЧИ: перенос занятия учеником 14+ ПОРОЖДАЕТ уведомление
/// опекуну. Это требование, а не деталь: смысл порога — самостоятельность, а не
/// тайна.
TEST_F(TellingTheGuardianTest, MovingALessonAlwaysTellsTheGuardian) {
    Turns(14);
    ASSERT_TRUE(AsStudent(Action::kRescheduleLesson).allowed);

    EXPECT_EQ(Tell(IndependentAct::kLessonRescheduled), 1);

    ASSERT_EQ(heard_.size(), 1U) << "подросток перенёс занятие, а родитель не узнал";
    EXPECT_EQ(heard_.front().guardian, kGuardian);
    EXPECT_EQ(heard_.front().student, kStudent);
    EXPECT_EQ(heard_.front().act, pdr::events::identity::WardAct::kLessonRescheduled);
}

TEST_F(TellingTheGuardianTest, BothParentsAreTold) {
    world_.roles.Grant(kTenant, kSecondGuardian, Role::kGuardian);
    world_.guardianships.Establish(kTenant, kSecondGuardian, kStudent);

    EXPECT_EQ(Tell(IndependentAct::kLessonCancelled), 2);
    ASSERT_EQ(heard_.size(), 2U) << "второй родитель узнал о занятии из счёта";
}

/// ОБЯЗАТЕЛЬНОЕ ПРАВИЛО ЗАДАЧИ: опекун видит, что отзыв написан, и НЕ видит
/// текста. Места для текста в событии нет вовсе — это не забыли передать, это
/// нечем передать.
TEST_F(TellingTheGuardianTest, AReviewIsAFactAndNotAText) {
    Turns(14);
    ASSERT_TRUE(AsStudent(Action::kWriteReview).allowed);

    EXPECT_EQ(Tell(IndependentAct::kReviewWritten), 1);

    ASSERT_EQ(heard_.size(), 1U);
    EXPECT_EQ(heard_.front().act, pdr::events::identity::WardAct::kReviewWritten);
    EXPECT_EQ(Name(IndependentAct::kReviewWritten), "review_written");
}

TEST_F(TellingTheGuardianTest, EveryActIsTold) {
    for (const auto act : kEveryIndependentAct) {
        heard_.clear();
        EXPECT_EQ(Tell(act), 1) << Name(act);
        EXPECT_EQ(heard_.size(), 1U) << Name(act);
    }
}

TEST_F(TellingTheGuardianTest, AnAdultWithoutAGuardianTellsNobody) {
    testing::AccessWorld alone;
    const NotifyGuardianOfAct notify{alone.guardianships, alone.clock, bus_};

    const auto told = notify.Execute(
        NotifyGuardianOfActRequest{kTenant, kStudent, IndependentAct::kReviewWritten});

    ASSERT_TRUE(told.HasValue());
    EXPECT_EQ(told.Value(), 0);
    EXPECT_TRUE(heard_.empty());
}

/// Переход через порог: сообщают ОБЕИМ сторонам, и только в день перехода.
class ComingOfAgeTest : public ::testing::Test {
protected:
    ComingOfAgeTest() {
        world_.guardianships.Establish(kTenant, kGuardian, kStudent);
        world_.birth_dates.Put(kTenant, kStudent, kBornOn);
        bus_.Subscribe<pdr::events::identity::CapabilitiesWidened>(
            [this](const pdr::events::identity::CapabilitiesWidened& event) {
                heard_.push_back(event);
            });
    }

    int AnnounceAt(core::Instant moment) {
        world_.clock.SetNow(moment);
        const AnnounceComingOfAge announce{
            world_.guardianships, world_.birth_dates, world_.maturity, world_.clock, bus_};
        const auto said = announce.Execute(kTenant, kStudent);
        EXPECT_TRUE(said.HasValue());
        return said.HasValue() ? said.Value() : 0;
    }

    testing::AccessWorld world_;
    pdr::events::InMemoryBus bus_;
    std::vector<pdr::events::identity::CapabilitiesWidened> heard_;
};

TEST_F(ComingOfAgeTest, TheBirthdayIsAnnouncedAndTheDayBeforeIsNot) {
    const auto fourteen = AgeStatus::TurnsAt(kBornOn, 14);

    EXPECT_EQ(AnnounceAt(fourteen - Days(1)), 0) << "объявили за день до дня рождения";
    EXPECT_TRUE(heard_.empty());

    EXPECT_EQ(AnnounceAt(fourteen), 1);
    ASSERT_EQ(heard_.size(), 1U);
    EXPECT_EQ(heard_.front().threshold, Name(AgeThreshold::kSlotsAndReviews));
    EXPECT_EQ(heard_.front().years, 14);
}

/// Уведомление И ученику, И опекуну — одно событие, две строки очереди.
TEST_F(ComingOfAgeTest, BothSidesAreNamed) {
    AnnounceAt(AgeStatus::TurnsAt(kBornOn, 16));

    ASSERT_EQ(heard_.size(), 1U);
    EXPECT_EQ(heard_.front().student, kStudent);
    ASSERT_TRUE(heard_.front().guardian.has_value()) << "опекуну о переходе не сказали";
    EXPECT_EQ(*heard_.front().guardian, kGuardian);
    EXPECT_EQ(heard_.front().threshold, Name(AgeThreshold::kOwnPayments));
}

TEST_F(ComingOfAgeTest, AnOrdinaryDayIsSilent) {
    EXPECT_EQ(AnnounceAt(AgeStatus::TurnsAt(kBornOn, 14) + Days(40)), 0);
    EXPECT_TRUE(heard_.empty()) << "событие без повода: на такие перестают смотреть";
}

TEST_F(ComingOfAgeTest, MajorityIsAnnouncedToo) {
    EXPECT_EQ(AnnounceAt(AgeStatus::TurnsAt(kBornOn, 18)), 1);

    ASSERT_EQ(heard_.size(), 1U);
    EXPECT_EQ(heard_.front().threshold, Name(AgeThreshold::kMajority))
        << "совершеннолетие прошло молча, а у опекуна кончились уровни";
}

TEST_F(ComingOfAgeTest, AnUnknownBirthDateIsSilent) {
    testing::AccessWorld blank;
    const AnnounceComingOfAge announce{
        blank.guardianships, blank.birth_dates, blank.maturity, blank.clock, bus_};

    const auto said = announce.Execute(kTenant, kStudent);

    ASSERT_TRUE(said.HasValue());
    EXPECT_EQ(said.Value(), 0);
}

}  // namespace
}  // namespace pdr::identity
