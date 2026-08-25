#include "identity/application/revoke_guardianship.hpp"

#include <chrono>
#include <optional>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "builders/guardianship_builder.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/in_memory_bus.hpp"
#include "fakes/fake_clock.hpp"
#include "identity/application/contract_service.hpp"

namespace pdr::identity {
namespace {

using namespace std::chrono_literals;
using pdr::identity::testing::GuardianshipBuilder;

/// Фейк узкого порта — на весь тест хватает нескольких строк. Моков здесь нет
/// намеренно: проверяется поведение сценария, а не порядок вызовов.
class FakeGuardianships final : public ports::GuardianshipRepository {
public:
    explicit FakeGuardianships(std::optional<Guardianship> active) : active_{std::move(active)} {}

    std::optional<Guardianship> FindActive(const core::TenantId&,
                                           const core::PersonId&,
                                           const core::PersonId&) const override {
        return active_;
    }

    void Save(const Guardianship& guardianship) override {
        saved_.push_back(guardianship);
        if (!guardianship.IsActive()) {
            active_.reset();
        }
    }

    const std::vector<Guardianship>& Saved() const noexcept {
        return saved_;
    }

private:
    std::optional<Guardianship> active_;
    std::vector<Guardianship> saved_;
};

class RevokeGuardianshipTest : public ::testing::Test {
protected:
    Guardianship Granted() const {
        return GuardianshipBuilder{}
            .InTenant(tenant_)
            .Guardian(guardian_)
            .Student(student_)
            .GrantedAt(clock_.Now())
            .Build();
    }

    RevokeGuardianship::Request Request() const {
        return {tenant_, guardian_, student_};
    }

    pdr::testing::FakeClock clock_;
    pdr::events::InMemoryBus bus_;
    core::TenantId tenant_{pdr::testing::Numbered<core::TenantId>(1)};
    core::PersonId guardian_{pdr::testing::Numbered<core::PersonId>(10)};
    core::PersonId student_{pdr::testing::Numbered<core::PersonId>(20)};
};

TEST_F(RevokeGuardianshipTest, RevokingPublishesTheEventAndSaves) {
    FakeGuardianships guardianships{Granted()};

    std::vector<pdr::events::identity::GuardianshipRevoked> heard;
    bus_.Subscribe<pdr::events::identity::GuardianshipRevoked>(
        [&heard](const pdr::events::identity::GuardianshipRevoked& event) {
            heard.push_back(event);
        });

    clock_.Advance(24h);
    const RevokeGuardianship revoke{guardianships, clock_, bus_};
    const auto done = revoke.Execute(Request());

    ASSERT_TRUE(done.HasValue());
    ASSERT_EQ(guardianships.Saved().size(), 1U);
    EXPECT_FALSE(guardianships.Saved().front().IsActive());

    ASSERT_EQ(heard.size(), 1U);
    EXPECT_TRUE(heard.front().guardian == guardian_);
    EXPECT_TRUE(heard.front().student == student_);
    EXPECT_TRUE(heard.front().envelope.tenant == tenant_);
    EXPECT_TRUE(heard.front().envelope.occurred_at == clock_.Now());
}

TEST_F(RevokeGuardianshipTest, SecondRevokeReturnsRefusalAndPublishesNothing) {
    FakeGuardianships guardianships{Granted()};

    const RevokeGuardianship revoke{guardianships, clock_, bus_};
    ASSERT_TRUE(revoke.Execute(Request()).HasValue());

    const auto again = revoke.Execute(Request());

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Kind(), core::ErrorKind::kNotFound);
    EXPECT_EQ(again.Failure().Code(), "guardianship_not_found");
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(RevokeGuardianshipTest, ContractAnswersWhoMayActForWhom) {
    const FakeGuardianships guardianships{Granted()};
    const ContractService contract{guardianships};

    EXPECT_TRUE(contract.MayActFor(tenant_, guardian_, student_));

    EXPECT_TRUE(contract.MayActFor(tenant_, student_, student_));

    const FakeGuardianships without{std::nullopt};
    const ContractService strict{without};
    EXPECT_FALSE(strict.MayActFor(tenant_, guardian_, student_));
}

}  // namespace
}  // namespace pdr::identity
