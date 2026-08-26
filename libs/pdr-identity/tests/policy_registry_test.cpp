#include <optional>
#include <set>
#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "builders/access_world.hpp"
#include "builders/identifiers.hpp"
#include "identity/application/contract_service.hpp"
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
        Name(DenyReason::kNoPolicy),
    };

    EXPECT_EQ(codes.size(), 5U);
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
        roles_.Grant(tenant_, tutor_, Role::kTutor);
        roles_.Grant(tenant_, student_, Role::kStudent);
        roles_.Grant(tenant_, guardian_, Role::kGuardian);
    }

    Resource Lesson() const {
        return Resource{tenant_, tutor_, student_};
    }

    testing::FakeRoles roles_;
    testing::FakeFaults faults_;
    policies::PolicySet permissions_{faults_};

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId tutor_{Numbered<core::PersonId>(10)};
    core::PersonId student_{Numbered<core::PersonId>(20)};
    core::PersonId guardian_{Numbered<core::PersonId>(30)};
    core::PersonId stranger_{Numbered<core::PersonId>(40)};
};

class GuardianOf final : public ports::GuardianshipRepository {
public:
    GuardianOf(core::PersonId guardian, core::PersonId student) noexcept
        : guardian_{std::move(guardian)}, student_{std::move(student)} {}

    std::optional<Guardianship> FindActive(const core::TenantId& tenant,
                                           const core::PersonId& guardian,
                                           const core::PersonId& student) const override {
        if (guardian != guardian_ || student != student_) {
            return std::nullopt;
        }
        return Guardianship::Restore(
            tenant, guardian, student, core::Instant::FromUnixMicros(0), std::nullopt);
    }

    void Save(const Guardianship&) override {}

private:
    core::PersonId guardian_;
    core::PersonId student_;
};

TEST_F(ContractDecidesTest, TheTutorCancelsHisOwnLesson) {
    const GuardianOf guardianships{guardian_, student_};
    const ContractService contract{guardianships, roles_, permissions_};

    EXPECT_TRUE(contract.Decide(tenant_, tutor_, Action::kCancelLesson, Lesson()).allowed);
}

TEST_F(ContractDecidesTest, TheGuardianCancelsForTheirWard) {
    const GuardianOf guardianships{guardian_, student_};
    const ContractService contract{guardianships, roles_, permissions_};

    EXPECT_TRUE(contract.Decide(tenant_, guardian_, Action::kCancelLesson, Lesson()).allowed);
}

TEST_F(ContractDecidesTest, SomeoneElsesGuardianGetsNothing) {
    const GuardianOf guardianships{guardian_, Numbered<core::PersonId>(99)};
    const ContractService contract{guardianships, roles_, permissions_};

    const auto decision = contract.Decide(tenant_, guardian_, Action::kCancelLesson, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kNotYours);
}

TEST_F(ContractDecidesTest, AStrangerWithoutARoleIsRefusedForTheRole) {
    const GuardianOf guardianships{guardian_, student_};
    const ContractService contract{guardianships, roles_, permissions_};

    const auto decision = contract.Decide(tenant_, stranger_, Action::kCancelLesson, Lesson());

    EXPECT_FALSE(decision.allowed);
    EXPECT_EQ(decision.reason, DenyReason::kRoleMissing);
}

/// «Вправе действовать за ученика» — это тот же расчёт отношения, а не второе
/// правило рядом: иначе два ответа на один вопрос разойдутся на первой правке.
TEST_F(ContractDecidesTest, MayActForAgreesWithTheTie) {
    const GuardianOf guardianships{guardian_, student_};
    const ContractService contract{guardianships, roles_, permissions_};

    EXPECT_TRUE(contract.MayActFor(tenant_, student_, student_));
    EXPECT_TRUE(contract.MayActFor(tenant_, guardian_, student_));
    EXPECT_FALSE(contract.MayActFor(tenant_, tutor_, student_));
    EXPECT_FALSE(contract.MayActFor(tenant_, stranger_, student_));
}

}  // namespace
}  // namespace pdr::identity
