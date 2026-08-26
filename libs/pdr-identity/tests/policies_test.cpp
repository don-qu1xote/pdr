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

namespace pdr::identity::policies {
namespace {

using pdr::testing::Numbered;

constexpr std::array kEveryTie{Tie::kMine, Tie::kAboutMe, Tie::kMyWard, Tie::kNone};

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
    {Action::kBookLesson, Role::kGuardian, Tie::kMyWard},

    {Action::kCancelLesson, Role::kTutor, Tie::kMine},
    {Action::kCancelLesson, Role::kStudent, Tie::kAboutMe},
    {Action::kCancelLesson, Role::kGuardian, Tie::kMyWard},

    {Action::kRescheduleLesson, Role::kTutor, Tie::kMine},
    {Action::kRescheduleLesson, Role::kStudent, Tie::kAboutMe},
    {Action::kRescheduleLesson, Role::kGuardian, Tie::kMyWard},

    {Action::kViewSchedule, Role::kTutor, Tie::kMine},
    {Action::kViewSchedule, Role::kStudent, Tie::kAboutMe},
    {Action::kViewSchedule, Role::kGuardian, Tie::kMyWard},
    {Action::kViewSchedule, Role::kOwner, Tie::kMine},
    {Action::kViewSchedule, Role::kOwner, Tie::kAboutMe},
    {Action::kViewSchedule, Role::kOwner, Tie::kMyWard},
    {Action::kViewSchedule, Role::kOwner, Tie::kNone},

    {Action::kViewInvoice, Role::kStudent, Tie::kAboutMe},
    {Action::kViewInvoice, Role::kGuardian, Tie::kMyWard},
    {Action::kViewInvoice, Role::kTutor, Tie::kMine},
    {Action::kViewInvoice, Role::kOwner, Tie::kMine},
    {Action::kViewInvoice, Role::kOwner, Tie::kAboutMe},
    {Action::kViewInvoice, Role::kOwner, Tie::kMyWard},
    {Action::kViewInvoice, Role::kOwner, Tie::kNone},

    {Action::kPayInvoice, Role::kStudent, Tie::kAboutMe},
    {Action::kPayInvoice, Role::kGuardian, Tie::kMyWard},

    {Action::kIssueRefund, Role::kTutor, Tie::kMine},
    {Action::kIssueRefund, Role::kOwner, Tie::kMine},
    {Action::kIssueRefund, Role::kOwner, Tie::kAboutMe},
    {Action::kIssueRefund, Role::kOwner, Tie::kMyWard},
    {Action::kIssueRefund, Role::kOwner, Tie::kNone},

    {Action::kSetTariff, Role::kTutor, Tie::kMine},
    {Action::kSetTariff, Role::kOwner, Tie::kMine},
    {Action::kSetTariff, Role::kOwner, Tie::kAboutMe},
    {Action::kSetTariff, Role::kOwner, Tie::kMyWard},
    {Action::kSetTariff, Role::kOwner, Tie::kNone},

    {Action::kViewMaterial, Role::kTutor, Tie::kMine},
    {Action::kViewMaterial, Role::kStudent, Tie::kAboutMe},
    {Action::kViewMaterial, Role::kGuardian, Tie::kMyWard},

    {Action::kEditMaterial, Role::kTutor, Tie::kMine},
    {Action::kPublishMaterial, Role::kTutor, Tie::kMine},
    {Action::kAssignPlan, Role::kTutor, Tie::kMine},

    {Action::kViewProgress, Role::kStudent, Tie::kAboutMe},
    {Action::kViewProgress, Role::kGuardian, Tie::kMyWard},
    {Action::kViewProgress, Role::kTutor, Tie::kMine},

    {Action::kRecordAttempt, Role::kStudent, Tie::kAboutMe},

    {Action::kExportProgress, Role::kStudent, Tie::kAboutMe},
    {Action::kExportProgress, Role::kGuardian, Tie::kMyWard},

    {Action::kViewTenantProgress, Role::kOwner, Tie::kMine},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kAboutMe},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kMyWard},
    {Action::kViewTenantProgress, Role::kOwner, Tie::kNone},
};

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
    PolicyDecision Ask(Role role, Action action, Tie tie) const {
        const Subject subject{tenant_, person_, RoleSet{}.With(role), tie};
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
            const Subject nobody{tenant_, person_, RoleSet{}, tie};
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
        tenant_, person_, RoleSet::Of({Role::kTutor, Role::kGuardian}), Tie::kMyWard};

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
    EXPECT_EQ(TieBetween(Numbered<core::PersonId>(30), lesson, true), Tie::kMyWard);
    EXPECT_EQ(TieBetween(Numbered<core::PersonId>(30), lesson, false), Tie::kNone);

    const Resource of_the_office{tenant, std::nullopt, std::nullopt};
    EXPECT_EQ(TieBetween(tutor, of_the_office, true), Tie::kNone)
        << "опека без того, о ком данные, ничего не значит";
}

}  // namespace
}  // namespace pdr::identity::policies
