#include "identity/core/guardianship.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/guardianship_builder.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::identity::testing::GuardianshipBuilder;

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

    // Исходная связь не изменилась: отзыв вернул новое значение.
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

}  // namespace
}  // namespace pdr::identity
