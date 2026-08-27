#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "identity/application/policies/policy_set.hpp"
#include "identity/application/policies/subject.hpp"

namespace pdr::identity {
namespace {

using pdr::testing::Numbered;

/// ГЛАВНЫЙ ТЕСТ РЕЕСТРА. Действие, у которого нет политики, обязано ронять этот
/// прогон, а не тихо запрещаться в рантайме: тихий запрет выясняется по жалобам
/// через неделю после выкатки, и виноватым выглядит что угодно, кроме забытой
/// строки в наборе.
TEST(PolicyRegistry, EveryActionHasAPolicy) {
    const testing::FakeFaults faults;
    const policies::PolicySet permissions{faults};

    for (const auto action : kEveryAction) {
        EXPECT_TRUE(permissions.Covers(action))
            << "действие «" << Name(action) << "» заведено, а политики у него нет";
    }
}

/// Реестр обходится целиком: список `kEveryAction` не должен разойтись с самим
/// перечислением. Размер сверяет static_assert в заголовке, а различность —
/// этот тест: шестнадцать разных значений из шестнадцати возможных — это все.
TEST(PolicyRegistry, TheActionListCoversTheEnumWithoutRepeats) {
    std::set<std::string_view> codes;
    for (const auto action : kEveryAction) {
        codes.insert(Name(action));
    }

    EXPECT_EQ(codes.size(), kEveryAction.size()) << "в списке действий есть повторы";
    EXPECT_EQ(codes.count("boundary"), 0U) << "граница списка попала в список";
}

TEST(PolicyRegistry, ActionCodesReadBackIntoTheSameAction) {
    for (const auto action : kEveryAction) {
        const auto parsed = ParseAction(Name(action));

        ASSERT_TRUE(parsed.has_value()) << Name(action);
        EXPECT_EQ(*parsed, action);
    }
    EXPECT_FALSE(ParseAction("delete_everything").has_value());
    EXPECT_FALSE(ParseAction("boundary").has_value());
}

TEST(PolicyRegistry, DenyReasonsHaveDistinctCodes) {
    const std::set<std::string_view> codes{
        Name(DenyReason::kAllowed),
        Name(DenyReason::kForeignTenant),
        Name(DenyReason::kRoleMissing),
        Name(DenyReason::kNotYours),
        Name(DenyReason::kScopeMissing),
        Name(DenyReason::kStudentGrewUp),
        Name(DenyReason::kNoPolicy),
    };

    EXPECT_EQ(codes.size(), 7U) << "у двух причин отказа один код: человек не различит их";
}

/// ЗНАЧЕНИЕ ПО УМОЛЧАНИЮ — ЗАПРЕТ, и он не молчит.
///
/// Действия без политики в реестре нет (это проверено выше), поэтому здесь
/// спрашивается граница списка: набор обязан отказать И сообщить о поломке
/// настройки. Тихое разрешение стоило бы ровно столько, сколько стоит открытая
/// дверь, о которой никто не знает.
TEST(PolicyRegistry, AnActionWithoutAPolicyIsRefusedAndReported) {
    const testing::FakeFaults faults;
    const policies::PolicySet permissions{faults};

    const auto tenant = Numbered<core::TenantId>(1);
    const Subject subject{tenant,
                          Numbered<core::PersonId>(10),
                          RoleSet::Of({Role::kOwner, Role::kTutor}),
                          Tie::kMine};

    const auto decision = permissions.Decide(
        subject, Action::kBoundary, Resource{tenant, std::nullopt, std::nullopt});

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kNoPolicy);
    ASSERT_EQ(faults.Reported().size(), 1U) << "о поломке настройки никто не узнал";
    EXPECT_EQ(faults.Reported().front(), Action::kBoundary);
}

/// Граница арендатора проверяется ДО политики и одна на всех: повторять её в
/// каждой области значит завести четыре места, где её однажды забудут.
TEST(PolicyRegistry, AResourceFromAnotherOfficeIsRefusedBeforeAnyPolicy) {
    const testing::FakeFaults faults;
    const policies::PolicySet permissions{faults};

    const auto mine = Numbered<core::TenantId>(1);
    const auto theirs = Numbered<core::TenantId>(2);
    const auto person = Numbered<core::PersonId>(10);

    const Subject subject{mine, person, RoleSet{}.With(Role::kTutor), Tie::kMine};
    const auto decision =
        permissions.Decide(subject, Action::kViewSchedule, Resource{theirs, person, std::nullopt});

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kForeignTenant)
        << "чужой кабинет объяснили ролью, а не границей";
    EXPECT_TRUE(faults.Reported().empty()) << "чужой кабинет засчитали за поломку настройки";
}

/// Контракт собирает субъекта сам: спрашивающий называет СЕБЯ и не приносит ни
/// ролей, ни связей. Иначе хендлер сначала узнаёт роли, потом решает сам.
class ContractDecidesTest : public ::testing::Test {
protected:
    ContractDecidesTest() {
        world_.roles.Grant(tenant_, tutor_, Role::kTutor);
        world_.roles.Grant(tenant_, student_, Role::kStudent);
        world_.roles.Grant(tenant_, guardian_, Role::kGuardian);
        world_.guardianships.Establish(tenant_, guardian_, student_);
        world_.Open(tenant_, guardian_, student_, GuardianScope::kSchedule, tutor_);
    }

    Resource Lesson() const {
        return Resource{tenant_, tutor_, student_};
    }

    testing::AccessWorld world_;

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId tutor_{Numbered<core::PersonId>(10)};
    core::PersonId student_{Numbered<core::PersonId>(20)};
    core::PersonId guardian_{Numbered<core::PersonId>(30)};
    core::PersonId stranger_{Numbered<core::PersonId>(40)};
};

TEST_F(ContractDecidesTest, TheTutorCancelsHisOwnLesson) {
    EXPECT_TRUE(world_.contract.Decide(tenant_, tutor_, Action::kCancelLesson, Lesson()).allowed);
}

TEST_F(ContractDecidesTest, TheGuardianCancelsForTheirWard) {
    EXPECT_TRUE(
        world_.contract.Decide(tenant_, guardian_, Action::kCancelLesson, Lesson()).allowed);
}

/// Тот же опекун, тот же подопечный — но уровень «деньги» ему не открывали.
TEST_F(ContractDecidesTest, AGuardianWithoutTheLevelIsRefused) {
    const auto decision = world_.contract.Decide(tenant_, guardian_, Action::kPayInvoice, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kScopeMissing);
}

TEST_F(ContractDecidesTest, SomeoneElsesGuardianGetsNothing) {
    const auto outsider = Numbered<core::PersonId>(99);
    world_.roles.Grant(tenant_, outsider, Role::kGuardian);

    const auto decision =
        world_.contract.Decide(tenant_, outsider, Action::kCancelLesson, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kNotYours);
}

TEST_F(ContractDecidesTest, AStrangerWithoutARoleIsRefusedForTheRole) {
    const auto decision =
        world_.contract.Decide(tenant_, stranger_, Action::kCancelLesson, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kRoleMissing);
}

/// «Вправе действовать за ученика» — это тот же расчёт отношения, а не второе
/// правило рядом: иначе два ответа на один вопрос разойдутся на первой правке.
TEST_F(ContractDecidesTest, MayActForAgreesWithTheTie) {
    EXPECT_TRUE(world_.contract.MayActFor(tenant_, student_, student_));
    EXPECT_TRUE(world_.contract.MayActFor(tenant_, guardian_, student_));
    EXPECT_FALSE(world_.contract.MayActFor(tenant_, tutor_, student_));
    EXPECT_FALSE(world_.contract.MayActFor(tenant_, stranger_, student_));
}

}  // namespace
}  // namespace pdr::identity
