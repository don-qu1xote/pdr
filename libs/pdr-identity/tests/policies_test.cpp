#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/policy_set.hpp"
#include "identity/application/policies/subject.hpp"
#include "identity/core/capabilities.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity::policies {
namespace {

using pdr::testing::Numbered;

constexpr std::array kEveryTie{Tie::kMine, Tie::kAboutMe, Tie::kInMyCare, Tie::kNone};

struct Grant final {
    Action action;
    Role role;
    Tie tie;
};

/// ЧТО РАЗРЕШЕНО. Всё, чего здесь нет, обязано быть запрещено, и это
/// проверяется перебором всех двухсот пятидесяти шести сочетаний, а не
/// перечислением отказов: список отказов такой длины никто не прочитает, а
/// дыру в нём не заметит.
///
/// Список написан здесь заново, а не взят из политик: тест, читающий ту же
/// таблицу, что и код, проверяет только то, что таблица равна себе.
const std::vector<Grant> kGranted{
    {Action::kBookLesson, Role::kTutor, Tie::kMine},
    {Action::kBookLesson, Role::kStudent, Tie::kAboutMe},
    {Action::kBookLesson, Role::kGuardian, Tie::kInMyCare},

    {Action::kCancelLesson, Role::kTutor, Tie::kMine},
    {Action::kCancelLesson, Role::kStudent, Tie::kAboutMe},
    {Action::kCancelLesson, Role::kGuardian, Tie::kInMyCare},

    {Action::kRescheduleLesson, Role::kTutor, Tie::kMine},
    {Action::kRescheduleLesson, Role::kStudent, Tie::kAboutMe},
    {Action::kRescheduleLesson, Role::kGuardian, Tie::kInMyCare},

    {Action::kViewSchedule, Role::kTutor, Tie::kMine},
    {Action::kViewSchedule, Role::kStudent, Tie::kAboutMe},
    {Action::kViewSchedule, Role::kGuardian, Tie::kInMyCare},
    {Action::kViewSchedule, Role::kOwner, Tie::kMine},
    {Action::kViewSchedule, Role::kOwner, Tie::kAboutMe},
    {Action::kViewSchedule, Role::kOwner, Tie::kInMyCare},
    {Action::kViewSchedule, Role::kOwner, Tie::kNone},

    {Action::kViewInvoice, Role::kStudent, Tie::kAboutMe},
    {Action::kViewInvoice, Role::kGuardian, Tie::kInMyCare},
    {Action::kViewInvoice, Role::kTutor, Tie::kMine},
    {Action::kViewInvoice, Role::kOwner, Tie::kMine},
    {Action::kViewInvoice, Role::kOwner, Tie::kAboutMe},
    {Action::kViewInvoice, Role::kOwner, Tie::kInMyCare},
    {Action::kViewInvoice, Role::kOwner, Tie::kNone},

    {Action::kPayInvoice, Role::kStudent, Tie::kAboutMe},
    {Action::kPayInvoice, Role::kGuardian, Tie::kInMyCare},

    {Action::kIssueRefund, Role::kTutor, Tie::kMine},
    {Action::kIssueRefund, Role::kOwner, Tie::kMine},
    {Action::kIssueRefund, Role::kOwner, Tie::kAboutMe},
    {Action::kIssueRefund, Role::kOwner, Tie::kInMyCare},
    {Action::kIssueRefund, Role::kOwner, Tie::kNone},

    {Action::kSetTariff, Role::kTutor, Tie::kMine},
    {Action::kSetTariff, Role::kOwner, Tie::kMine},
    {Action::kSetTariff, Role::kOwner, Tie::kAboutMe},
    {Action::kSetTariff, Role::kOwner, Tie::kInMyCare},
    {Action::kSetTariff, Role::kOwner, Tie::kNone},

    {Action::kViewMaterial, Role::kTutor, Tie::kMine},
    {Action::kViewMaterial, Role::kStudent, Tie::kAboutMe},
    {Action::kViewMaterial, Role::kGuardian, Tie::kInMyCare},

    {Action::kEditMaterial, Role::kTutor, Tie::kMine},
    {Action::kPublishMaterial, Role::kTutor, Tie::kMine},
    {Action::kAssignPlan, Role::kTutor, Tie::kMine},

    {Action::kViewProgress, Role::kStudent, Tie::kAboutMe},
    {Action::kViewProgress, Role::kGuardian, Tie::kInMyCare},
    {Action::kViewProgress, Role::kTutor, Tie::kMine},

    {Action::kRecordAttempt, Role::kStudent, Tie::kAboutMe},

    {Action::kExportProgress, Role::kStudent, Tie::kAboutMe},
    {Action::kExportProgress, Role::kGuardian, Tie::kInMyCare},

    {Action::kViewTenantProgress, Role::kOwner, Tie::kMine},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kAboutMe},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kInMyCare},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kNone},

    {Action::kViewLessonRecording, Role::kStudent, Tie::kAboutMe},
    {Action::kViewLessonRecording, Role::kTutor, Tie::kMine},
    {Action::kViewLessonRecording, Role::kGuardian, Tie::kInMyCare},

    {Action::kViewLessonTranscript, Role::kStudent, Tie::kAboutMe},
    {Action::kViewLessonTranscript, Role::kTutor, Tie::kMine},
    {Action::kViewLessonTranscript, Role::kGuardian, Tie::kInMyCare},

    {Action::kViewAccessJournal, Role::kStudent, Tie::kAboutMe},
    {Action::kViewAccessJournal, Role::kTutor, Tie::kMine},

    {Action::kManageGuardianAccess, Role::kStudent, Tie::kAboutMe},
    {Action::kManageGuardianAccess, Role::kTutor, Tie::kMine},

    {Action::kWriteReview, Role::kStudent, Tie::kAboutMe},

    {Action::kManageAutoPayment, Role::kGuardian, Tie::kInMyCare},

    {Action::kInvitePeople, Role::kOwner, Tie::kMine},
    {Action::kInvitePeople, Role::kOwner, Tie::kAboutMe},
    {Action::kInvitePeople, Role::kOwner, Tie::kInMyCare},
    {Action::kInvitePeople, Role::kOwner, Tie::kNone},
    {Action::kInvitePeople, Role::kTutor, Tie::kMine},
    {Action::kInvitePeople, Role::kTutor, Tie::kAboutMe},
    {Action::kInvitePeople, Role::kTutor, Tie::kInMyCare},
    {Action::kInvitePeople, Role::kTutor, Tie::kNone},

    {Action::kManagePractice, Role::kOwner, Tie::kMine},
    {Action::kManagePractice, Role::kOwner, Tie::kAboutMe},
    {Action::kManagePractice, Role::kOwner, Tie::kInMyCare},
    {Action::kManagePractice, Role::kOwner, Tie::kNone},
};

/// ЧТО ОТКРЫВАЕТ КАЖДЫЙ УРОВЕНЬ. Тоже написано заново: уровень, поехавший на
/// соседнюю область, из кода не виден — там это одна строка в наборе `AnyOf`.
struct Level final {
    GuardianScope scope;
    std::vector<Action> opens;
};

const std::vector<Level> kLevels{
    {GuardianScope::kSchedule,
     {Action::kBookLesson,
      Action::kCancelLesson,
      Action::kRescheduleLesson,
      Action::kViewSchedule}},
    {GuardianScope::kPayments,
     {Action::kViewInvoice, Action::kPayInvoice, Action::kManageAutoPayment}},
    {GuardianScope::kNotesAndHomework,
     {Action::kViewMaterial, Action::kViewProgress, Action::kExportProgress}},
    {GuardianScope::kRecordings, {Action::kViewLessonRecording, Action::kViewLessonTranscript}},
};

bool Opens(const Level& level, Action action) {
    return std::find(level.opens.begin(), level.opens.end(), action) != level.opens.end();
}

bool IsGranted(Action action, Role role, Tie tie) {
    return std::any_of(kGranted.begin(), kGranted.end(), [&](const Grant& grant) {
        return grant.action == action && grant.role == role && grant.tie == tie;
    });
}

/// Участвует ли роль в правиле этого действия хоть при каком-нибудь отношении.
/// Отсюда берётся ОЖИДАЕМАЯ ПРИЧИНА отказа: роль участвует — значит, отказали
/// не за роль, а за то, что ресурс чужой.
bool RoleInvolved(Action action, Role role) {
    return std::any_of(kGranted.begin(), kGranted.end(), [&](const Grant& grant) {
        return grant.action == action && grant.role == role;
    });
}

class PoliciesTest : public ::testing::Test {
protected:
    /// Уровни опекуна и возможности по возрасту открыты все: этот набор
    /// проверяет РОЛЬ И ОТНОШЕНИЕ, а про уровни и про возраст есть свои наборы —
    /// ниже и в capabilities_test.cpp.
    PolicyDecision Ask(Role role, Action action, Tie tie) const {
        return Ask(role, action, tie, GuardianScopeSet::Everything());
    }

    PolicyDecision Ask(Role role, Action action, Tie tie, GuardianScopeSet scopes) const {
        const Subject subject{tenant_,
                              person_,
                              RoleSet{}.With(role),
                              tie,
                              GuardianAccess{scopes, GuardianScopeSet{}, GuardianScopeSet{}},
                              Capabilities::Everything()};
        return permissions_.Decide(subject, action, Resource{tenant_, std::nullopt, std::nullopt});
    }

    std::string Where(Action action, Role role, Tie tie) const {
        return std::string{Name(action)} + " / " + std::string{Name(role)} + " / " +
               std::string{Name(tie)};
    }

    testing::FakeFaults faults_;
    PolicySet permissions_{faults_};
    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId person_{Numbered<core::PersonId>(10)};
};

/// ГЛАВНЫЙ ТЕСТ. Все действия на все роли при всех отношениях: разрешено ровно
/// то, что перечислено, и ни клеткой больше.
TEST_F(PoliciesTest, EveryRoleOnEveryActionAnswersExactlyAsWritten) {
    for (const auto action : kEveryAction) {
        for (const auto role : kEveryRole) {
            for (const auto tie : kEveryTie) {
                const auto decision = Ask(role, action, tie);
                const bool expected = IsGranted(action, role, tie);

                EXPECT_EQ(decision.allowed, expected) << Where(action, role, tie);
                if (expected) {
                    EXPECT_EQ(decision.reason, DenyReason::kAllowed) << Where(action, role, tie);
                }
            }
        }
    }
}

/// Отказ несёт ПОЛЕЗНУЮ причину: «нет роли» и «это не ваше» — разные ответы,
/// после которых человек делает разное.
TEST_F(PoliciesTest, RefusalTellsWhichOfTheTwoWentWrong) {
    for (const auto action : kEveryAction) {
        for (const auto role : kEveryRole) {
            for (const auto tie : kEveryTie) {
                if (IsGranted(action, role, tie)) {
                    continue;
                }

                const auto expected =
                    RoleInvolved(action, role) ? DenyReason::kNotYours : DenyReason::kRoleMissing;
                EXPECT_EQ(Ask(role, action, tie).reason, expected) << Where(action, role, tie);
            }
        }
    }
}

TEST_F(PoliciesTest, NobodyWithoutARoleGetsAnything) {
    for (const auto action : kEveryAction) {
        for (const auto tie : kEveryTie) {
            const Subject nobody{
                tenant_,
                person_,
                RoleSet{},
                tie,
                GuardianAccess{
                    GuardianScopeSet::Everything(), GuardianScopeSet{}, GuardianScopeSet{}}};
            const auto decision =
                permissions_.Decide(nobody, action, Resource{tenant_, std::nullopt, std::nullopt});

            EXPECT_FALSE(decision.allowed) << Name(action);
            EXPECT_EQ(decision.reason, DenyReason::kRoleMissing) << Name(action);
        }
    }
}

/// СУПЕР-АДМИНА НЕТ. Владелец школы ведёт школу, но не действует за людей:
/// записать, отменить, перенести, оплатить, записать попытку и править чужой
/// материал он не может ни при каком отношении.
TEST_F(PoliciesTest, TheSchoolOwnerIsNotASuperuser) {
    const std::vector<Action> never{
        Action::kBookLesson,
        Action::kCancelLesson,
        Action::kRescheduleLesson,
        Action::kPayInvoice,
        Action::kViewMaterial,
        Action::kEditMaterial,
        Action::kPublishMaterial,
        Action::kAssignPlan,
        Action::kViewProgress,
        Action::kRecordAttempt,
        Action::kExportProgress,
        Action::kViewLessonRecording,
        Action::kViewLessonTranscript,
        Action::kViewAccessJournal,
        Action::kManageGuardianAccess,
    };

    for (const auto action : never) {
        for (const auto tie : kEveryTie) {
            EXPECT_FALSE(Ask(Role::kOwner, action, tie).allowed)
                << "владелец школы получил «" << Name(action) << "»";
        }
    }
}

/// Несколько ролей у одного человека — норма: репетитор, который у соседа
/// родитель. Права складываются, а не выбирается «главная».
TEST_F(PoliciesTest, SeveralRolesAddUp) {
    const Subject both{
        tenant_,
        person_,
        RoleSet::Of({Role::kTutor, Role::kGuardian}),
        Tie::kInMyCare,
        GuardianAccess{GuardianScopeSet::Everything(), GuardianScopeSet{}, GuardianScopeSet{}}};

    const auto decision = permissions_.Decide(
        both, Action::kBookLesson, Resource{tenant_, std::nullopt, std::nullopt});

    EXPECT_TRUE(decision.allowed)
        << "роль опекуна перестала работать оттого, что человек ещё и репетитор";
}

TEST_F(PoliciesTest, RoleSetRemembersWhatWasPutIn) {
    const auto set = RoleSet::Of({Role::kTutor, Role::kOwner});

    EXPECT_TRUE(set.Has(Role::kTutor));
    EXPECT_TRUE(set.Has(Role::kOwner));
    EXPECT_FALSE(set.Has(Role::kStudent));
    EXPECT_FALSE(set.Has(Role::kGuardian));
    EXPECT_FALSE(set.Empty());
    EXPECT_TRUE(RoleSet{}.Empty());
}

/// НИ ОДИН УРОВЕНЬ НЕ ОТКРЫВАЕТСЯ САМ. Всё, что опекуну вообще даётся, при
/// пустом наборе уровней запрещено — и запрещено ИМЕННО ЗА УРОВЕНЬ, а не за
/// роль: иначе человек пойдёт выпрашивать роль, которая у него и так есть.
TEST_F(PoliciesTest, NothingIsOpenToAGuardianWithoutConsent) {
    int checked = 0;
    for (const auto action : kEveryAction) {
        if (!Ask(Role::kGuardian, action, Tie::kInMyCare, GuardianScopeSet::Everything()).allowed) {
            continue;
        }
        ++checked;

        const auto decision = Ask(Role::kGuardian, action, Tie::kInMyCare, GuardianScopeSet{});
        EXPECT_FALSE(decision.allowed) << Name(action);
        EXPECT_EQ(decision.reason, DenyReason::kScopeMissing) << Name(action);
    }

    EXPECT_GT(checked, 0) << "опекуну не даётся ничего: проверять нечего";
}

/// UNIT ПО КАЖДОМУ УРОВНЮ. Включён ровно один — и открылось ровно то, что этим
/// уровнем называется. Отказ по остальным назван уровнем, а не ролью: человек,
/// услышавший «нет роли», пойдёт выпрашивать роль, которая у него и так есть.
TEST_F(PoliciesTest, EachLevelOpensExactlyItsOwnActions) {
    ASSERT_EQ(kLevels.size(), kEveryGuardianScope.size())
        << "уровень завели, а что он открывает — не написали";

    for (const auto& level : kLevels) {
        const auto only = GuardianScopeSet{}.With(level.scope);

        for (const auto action : kEveryAction) {
            const auto decision = Ask(Role::kGuardian, action, Tie::kInMyCare, only);
            const bool expected = Opens(level, action);
            const std::string where =
                std::string{Name(level.scope)} + " / " + std::string{Name(action)};

            EXPECT_EQ(decision.allowed, expected) << where;
            if (!expected && IsGranted(action, Role::kGuardian, Tie::kInMyCare)) {
                EXPECT_EQ(decision.reason, DenyReason::kScopeMissing) << where;
            }
        }
    }
}

/// ЗАПИСИ ЗАНЯТИЙ НЕ ОТКРЫВАЮТСЯ ВМЕСТЕ С ОПЕКОЙ. Набор, который выдаётся при
/// заведении опеки, слушать урок не позволяет — на это нужно отдельное
/// согласие.
TEST_F(PoliciesTest, RecordingsStayShutWhenGuardianshipIsEstablished) {
    const auto by_default = GuardianScopeSet::OpenedByGuardianship();

    EXPECT_FALSE(by_default.Has(GuardianScope::kRecordings));
    EXPECT_TRUE(by_default.Has(GuardianScope::kSchedule));
    EXPECT_TRUE(by_default.Has(GuardianScope::kPayments));
    EXPECT_TRUE(by_default.Has(GuardianScope::kNotesAndHomework));

    for (const auto action : {Action::kViewLessonRecording, Action::kViewLessonTranscript}) {
        const auto decision = Ask(Role::kGuardian, action, Tie::kInMyCare, by_default);

        EXPECT_FALSE(decision.allowed) << "запись занятия открылась вместе с опекой";
        EXPECT_EQ(decision.reason, DenyReason::kScopeMissing);
    }
}

/// Уровни не подменяют друг друга: расписание не открывает записей.
TEST_F(PoliciesTest, OneLevelDoesNotOpenAnother) {
    const auto schedule_only = GuardianScopeSet{}.With(GuardianScope::kSchedule);

    EXPECT_TRUE(Ask(Role::kGuardian, Action::kViewSchedule, Tie::kInMyCare, schedule_only).allowed);
    EXPECT_FALSE(
        Ask(Role::kGuardian, Action::kViewLessonRecording, Tie::kInMyCare, schedule_only).allowed);
    EXPECT_FALSE(Ask(Role::kGuardian, Action::kPayInvoice, Tie::kInMyCare, schedule_only).allowed);
    EXPECT_FALSE(
        Ask(Role::kGuardian, Action::kViewProgress, Tie::kInMyCare, schedule_only).allowed);
}

/// «Ученик вырос» — отдельная причина, а не «уровень не открыли»: идти надо не
/// к репетитору, а к самому ученику.
TEST_F(PoliciesTest, AGrownStudentIsANamedReason) {
    const Subject grown{tenant_,
                        person_,
                        RoleSet{}.With(Role::kGuardian),
                        Tie::kInMyCare,
                        GuardianAccess{GuardianScopeSet{},
                                       GuardianScopeSet{},
                                       GuardianScopeSet{}.With(GuardianScope::kRecordings)}};

    const auto decision = permissions_.Decide(
        grown, Action::kViewLessonRecording, Resource{tenant_, std::nullopt, std::nullopt});

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kStudentGrewUp);
}

/// Опекун не смотрит журнал доступа к своему подопечному ни при каком уровне.
/// Иначе он видит, заметил ли ребёнок его просмотры, и гарантия перестаёт быть
/// гарантией.
TEST_F(PoliciesTest, TheJournalIsNotForTheGuardian) {
    for (const auto tie : kEveryTie) {
        EXPECT_FALSE(
            Ask(Role::kGuardian, Action::kViewAccessJournal, tie, GuardianScopeSet::Everything())
                .allowed)
            << "опекун читает журнал доступа к ребёнку";
    }
}

/// Опекун не открывает уровни сам себе.
TEST_F(PoliciesTest, AGuardianDoesNotConsentForHimself) {
    for (const auto tie : kEveryTie) {
        EXPECT_FALSE(
            Ask(Role::kGuardian, Action::kManageGuardianAccess, tie, GuardianScopeSet::Everything())
                .allowed)
            << "опекун сам себе открыл доступ";
    }
}

/// Комбинаторы — не украшение: `AllOf` останавливается на первом отказе и
/// отдаёт ЕГО причину, `AnyOf` из нескольких отказов выбирает самый полезный.
TEST(Combinators, AllOfStopsAtTheFirstRefusalAndKeepsItsReason) {
    const AllOf rule{HasRole{Role::kTutor}, Tied{Tie::kMine}};
    const core::TenantId tenant = Numbered<core::TenantId>(1);
    const Resource nothing{tenant, std::nullopt, std::nullopt};

    const Subject stranger{tenant, Numbered<core::PersonId>(10), RoleSet{}, Tie::kMine};
    EXPECT_EQ(rule.Decide(stranger, Action::kBookLesson, nothing).reason, DenyReason::kRoleMissing);

    const Subject tutor{
        tenant, Numbered<core::PersonId>(10), RoleSet{}.With(Role::kTutor), Tie::kNone};
    EXPECT_EQ(rule.Decide(tutor, Action::kBookLesson, nothing).reason, DenyReason::kNotYours);

    const Subject his{
        tenant, Numbered<core::PersonId>(10), RoleSet{}.With(Role::kTutor), Tie::kMine};
    EXPECT_TRUE(rule.Decide(his, Action::kBookLesson, nothing).allowed);
}

TEST(Combinators, AnyOfPicksTheMostUsefulRefusal) {
    const AnyOf rule{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}},
                     AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}}};
    const core::TenantId tenant = Numbered<core::TenantId>(1);
    const Resource nothing{tenant, std::nullopt, std::nullopt};

    const Subject tutor{
        tenant, Numbered<core::PersonId>(10), RoleSet{}.With(Role::kTutor), Tie::kNone};

    EXPECT_EQ(rule.Decide(tutor, Action::kBookLesson, nothing).reason, DenyReason::kNotYours)
        << "репетитору сказали «нет роли ученика» вместо «это не ваше занятие»";

    const Subject nobody{tenant, Numbered<core::PersonId>(10), RoleSet{}, Tie::kNone};
    EXPECT_EQ(rule.Decide(nobody, Action::kBookLesson, nothing).reason, DenyReason::kRoleMissing);
}

TEST(Ties, TheClosestTieWins) {
    const core::TenantId tenant = Numbered<core::TenantId>(1);
    const auto tutor = Numbered<core::PersonId>(10);
    const auto student = Numbered<core::PersonId>(20);
    const Resource lesson{tenant, tutor, student};

    EXPECT_EQ(TieBetween(tutor, lesson, false), Tie::kMine);
    EXPECT_EQ(TieBetween(student, lesson, false), Tie::kAboutMe);
    EXPECT_EQ(TieBetween(Numbered<core::PersonId>(30), lesson, true), Tie::kInMyCare);
    EXPECT_EQ(TieBetween(Numbered<core::PersonId>(30), lesson, false), Tie::kNone);

    const Resource of_the_office{tenant, std::nullopt, std::nullopt};
    EXPECT_EQ(TieBetween(tutor, of_the_office, true), Tie::kNone)
        << "опека без того, о ком данные, ничего не значит";
}

}  // namespace
}  // namespace pdr::identity::policies
