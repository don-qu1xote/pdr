#include "identity/core/guardianship.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/guardianship_builder.hpp"
#include "builders/identifiers.hpp"
#include "builders/moment_builder.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/tenant.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::identity::testing::GuardianshipBuilder;
using pdr::testing::MomentBuilder;

TEST(Guardianship, GrantedIsActive) {
    const auto granted = GuardianshipBuilder{}.Build();

    EXPECT_TRUE(granted.IsActive());
    EXPECT_FALSE(granted.RevokedAt().has_value());
}

TEST(Guardianship, RevokeKeepsTheLinkAndItsDate) {
    const auto granted = GuardianshipBuilder{}.Build();
    const auto at = granted.GrantedAt() + 72h;

    const auto revoked = granted.Revoked(at);

    ASSERT_TRUE(revoked.HasValue());
    EXPECT_FALSE(revoked.Value().IsActive());
    ASSERT_TRUE(revoked.Value().RevokedAt().has_value());
    EXPECT_TRUE(*revoked.Value().RevokedAt() == at);
    EXPECT_TRUE(revoked.Value().Guardian() == granted.Guardian());
    EXPECT_TRUE(revoked.Value().Student() == granted.Student());

    EXPECT_TRUE(granted.IsActive());
}

TEST(Guardianship, SecondRevokeIsARefusalAndNotACrash) {
    const auto granted = GuardianshipBuilder{}.Build();
    const auto once = granted.Revoked(granted.GrantedAt() + 1h);
    ASSERT_TRUE(once.HasValue());

    const auto twice = once.Value().Revoked(granted.GrantedAt() + 2h);

    ASSERT_FALSE(twice.HasValue());
    EXPECT_EQ(twice.Failure().Kind(), core::ErrorKind::kConflict);
    EXPECT_EQ(twice.Failure().Code(), "guardianship_already_revoked");
}

TEST(Guardianship, AlreadyRevokedLinkIsRestoredAsRevoked) {
    const auto granted_at = GuardianshipBuilder{}.Build().GrantedAt();
    const auto restored = GuardianshipBuilder{}.RevokedAt(granted_at + 5h).Build();

    EXPECT_FALSE(restored.IsActive());
    ASSERT_TRUE(restored.RevokedAt().has_value());
    EXPECT_TRUE(*restored.RevokedAt() == granted_at + 5h);
}

TEST(Guardianship, RevokeBeforeGrantIsRefused) {
    const auto granted = GuardianshipBuilder{}.Build();

    const auto refused = granted.Revoked(granted.GrantedAt() - 1h);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "guardianship_revoked_before_granted");
}

/// Ученик не может быть опекуном самому себе. Отказ, а не исключение: такое
/// приходит из формы, где два поля заполнены одним человеком.
TEST(Guardianship, SelfGuardianshipIsRefused) {
    const auto person = pdr::testing::Numbered<core::PersonId>(5);

    const auto refused = Guardianship::Grant(pdr::testing::Numbered<core::TenantId>(1),
                                             person,
                                             person,
                                             MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "guardianship_self");
}

TEST(Guardianship, GrantedBetweenTwoPeopleIsActive) {
    const auto granted = Guardianship::Grant(pdr::testing::Numbered<core::TenantId>(1),
                                             pdr::testing::Numbered<core::PersonId>(10),
                                             pdr::testing::Numbered<core::PersonId>(20),
                                             MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build());

    ASSERT_TRUE(granted.HasValue());
    EXPECT_TRUE(granted.Value().IsActive());
}

/// Опека живёт внутри одного арендатора. Участия приходят целиком именно
/// затем, чтобы «опекун из чужого арендатора» было выразимо и отвергнуто.
TEST(Guardianship, LinkAcrossTenantsIsRefused) {
    const auto here = Tenant::Compose(pdr::testing::Numbered<core::TenantId>(1), "Свой").Value();
    const auto there = Tenant::Compose(pdr::testing::Numbered<core::TenantId>(2), "Чужой").Value();

    const auto guardian =
        TenantMembership::In(here, pdr::testing::Numbered<core::PersonId>(10), Role::kGuardian);
    const auto student =
        TenantMembership::In(there, pdr::testing::Numbered<core::PersonId>(20), Role::kStudent);

    const auto refused = Guardianship::Establish(
        guardian, student, MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "guardianship_across_tenants");
}

TEST(Guardianship, EstablishedBetweenMembersOfOneTenant) {
    const auto tenant =
        Tenant::Compose(pdr::testing::Numbered<core::TenantId>(1), "Мария Петровна").Value();
    const auto guardian =
        TenantMembership::In(tenant, pdr::testing::Numbered<core::PersonId>(10), Role::kGuardian);
    const auto student =
        TenantMembership::In(tenant, pdr::testing::Numbered<core::PersonId>(20), Role::kStudent);
    const auto at = MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build();

    const auto established = Guardianship::Establish(guardian, student, at);

    ASSERT_TRUE(established.HasValue());
    EXPECT_TRUE(established.Value().Tenant() == tenant.Id());
    EXPECT_TRUE(established.Value().Guardian() == guardian.Person());
    EXPECT_TRUE(established.Value().Student() == student.Person());
    EXPECT_TRUE(established.Value().IsActive());
}

TEST(Guardianship, RolesAreCheckedOnBothSides) {
    const auto tenant = Tenant::Compose(pdr::testing::Numbered<core::TenantId>(1), "Свой").Value();
    const auto at = MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build();
    const auto guardian =
        TenantMembership::In(tenant, pdr::testing::Numbered<core::PersonId>(10), Role::kGuardian);
    const auto student =
        TenantMembership::In(tenant, pdr::testing::Numbered<core::PersonId>(20), Role::kStudent);
    const auto tutor =
        TenantMembership::In(tenant, pdr::testing::Numbered<core::PersonId>(30), Role::kTutor);

    const auto not_a_guardian = Guardianship::Establish(tutor, student, at);
    ASSERT_FALSE(not_a_guardian.HasValue());
    EXPECT_EQ(not_a_guardian.Failure().Code(), "guardianship_guardian_role_missing");

    const auto not_a_student = Guardianship::Establish(guardian, tutor, at);
    ASSERT_FALSE(not_a_student.HasValue());
    EXPECT_EQ(not_a_student.Failure().Code(), "guardianship_student_role_missing");
}

/// Тот же человек — опекун своему ребёнку у одного репетитора и ученик у
/// другого. Две роли не мешают ни одной из связей.
TEST(Guardianship, GuardianElsewhereIsStillAStudentHere) {
    const auto school = Tenant::Compose(pdr::testing::Numbered<core::TenantId>(1), "Школа").Value();
    const auto courses =
        Tenant::Compose(pdr::testing::Numbered<core::TenantId>(2), "Курсы").Value();
    const auto adult = pdr::testing::Numbered<core::PersonId>(7);
    const auto at = MomentBuilder{}.Utc(2026, 1, 15).At(9, 0).Build();

    const auto as_guardian = TenantMembership::In(school, adult, Role::kGuardian);
    const auto child =
        TenantMembership::In(school, pdr::testing::Numbered<core::PersonId>(8), Role::kStudent);
    const auto as_student = TenantMembership::In(courses, adult, Role::kStudent);

    EXPECT_TRUE(Guardianship::Establish(as_guardian, child, at).HasValue());
    EXPECT_EQ(as_student.InRole(), Role::kStudent);
    EXPECT_FALSE(as_guardian.SameTenantAs(as_student));
}

}  // namespace
}  // namespace pdr::identity
