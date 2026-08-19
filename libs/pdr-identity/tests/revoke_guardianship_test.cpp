#include "identity/application/revoke_guardianship.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include "events/identity/guardianship_revoked.hpp"
#include "events/in_memory_bus.hpp"
#include "identity/application/contract_service.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::identity::Guardianship;
using pdr::identity::RevokeGuardianship;

/// Фейк узкого порта — на весь тест хватает нескольких строк.
class FakeGuardianships final : public pdr::identity::ports::GuardianshipRepository {
public:
    explicit FakeGuardianships(std::optional<Guardianship> active) : active_{std::move(active)} {}

    std::optional<Guardianship> FindActive(const pdr::core::TenantId&,
                                           const pdr::core::PersonId&,
                                           const pdr::core::PersonId&) const override {
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

struct Fixture final {
    pdr::testing::FakeIdGenerator ids;
    pdr::testing::FakeClock clock;
    pdr::core::TenantId tenant{ids.Next<pdr::core::TenantId>()};
    pdr::core::PersonId guardian{ids.Next<pdr::core::PersonId>()};
    pdr::core::PersonId student{ids.Next<pdr::core::PersonId>()};

    RevokeGuardianship::Request Request() const {
        return {tenant, guardian, student};
    }

    Guardianship Granted() const {
        return Guardianship::Grant(
            tenant, guardian, student, pdr::testing::FakeClock::DefaultStart());
    }
};

void RevokingPublishesTheEventAndSaves() {
    Fixture fixture;
    FakeGuardianships guardianships{fixture.Granted()};
    pdr::events::InMemoryBus bus;

    std::vector<pdr::events::identity::GuardianshipRevoked> heard;
    bus.Subscribe<pdr::events::identity::GuardianshipRevoked>(
        [&heard](const pdr::events::identity::GuardianshipRevoked& event) {
            heard.push_back(event);
        });

    fixture.clock.Advance(24h);
    const RevokeGuardianship revoke{guardianships, fixture.clock, bus};
    const auto done = revoke.Execute(fixture.Request());

    PDR_CHECK(done.HasValue());
    PDR_CHECK(guardianships.Saved().size() == 1);
    PDR_CHECK(!guardianships.Saved().front().IsActive());

    PDR_CHECK(heard.size() == 1);
    PDR_CHECK(heard.front().guardian == fixture.guardian);
    PDR_CHECK(heard.front().student == fixture.student);
    PDR_CHECK(heard.front().envelope.tenant == fixture.tenant);
    PDR_CHECK(heard.front().envelope.occurred_at == fixture.clock.Now());
}

void SecondRevokeReturnsRefusalAndPublishesNothing() {
    Fixture fixture;
    FakeGuardianships guardianships{fixture.Granted()};
    pdr::events::InMemoryBus bus;

    const RevokeGuardianship revoke{guardianships, fixture.clock, bus};
    PDR_CHECK(revoke.Execute(fixture.Request()).HasValue());

    const auto again = revoke.Execute(fixture.Request());

    PDR_CHECK(!again.HasValue());
    PDR_CHECK(again.Failure().Kind() == pdr::core::ErrorKind::kNotFound);
    PDR_CHECK(again.Failure().Code() == "guardianship_not_found");
    PDR_CHECK(bus.Published() == 1);
}

void ContractAnswersWhoMayActForWhom() {
    Fixture fixture;
    const FakeGuardianships guardianships{fixture.Granted()};
    const pdr::identity::ContractService contract{guardianships};

    PDR_CHECK(contract.MayActFor(fixture.tenant, fixture.guardian, fixture.student));

    // Самостоятельный взрослый ученик — сам себе представитель.
    PDR_CHECK(contract.MayActFor(fixture.tenant, fixture.student, fixture.student));

    const FakeGuardianships without{std::nullopt};
    const pdr::identity::ContractService strict{without};
    PDR_CHECK(!strict.MayActFor(fixture.tenant, fixture.guardian, fixture.student));
}

}  // namespace

int main() {
    RevokingPublishesTheEventAndSaves();
    SecondRevokeReturnsRefusalAndPublishesNothing();
    ContractAnswersWhoMayActForWhom();
    return pdr::testing::Summary("identity.revoke_guardianship");
}
