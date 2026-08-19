#include "identity/core/guardianship.hpp"

#include <chrono>

#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::identity::Guardianship;

Guardianship MakeGranted(const pdr::testing::FakeIdGenerator& ids, pdr::core::Instant at) {
    return Guardianship::Grant(ids.Next<pdr::core::TenantId>(),
                               ids.Next<pdr::core::PersonId>(),
                               ids.Next<pdr::core::PersonId>(),
                               at);
}

void GrantedGuardianshipIsActive() {
    const pdr::testing::FakeIdGenerator ids;
    const auto granted = MakeGranted(ids, pdr::testing::FakeClock::DefaultStart());

    PDR_CHECK(granted.IsActive());
    PDR_CHECK(!granted.RevokedAt().has_value());
}

void RevokeKeepsTheLinkAndItsDate() {
    const pdr::testing::FakeIdGenerator ids;
    const auto start = pdr::testing::FakeClock::DefaultStart();
    const auto granted = MakeGranted(ids, start);

    const auto revoked = granted.Revoked(start + 72h);

    PDR_CHECK(revoked.HasValue());
    PDR_CHECK(!revoked.Value().IsActive());
    PDR_CHECK(revoked.Value().RevokedAt().has_value());
    PDR_CHECK(*revoked.Value().RevokedAt() == start + 72h);
    PDR_CHECK(revoked.Value().Guardian() == granted.Guardian());
    PDR_CHECK(revoked.Value().Student() == granted.Student());

    // Исходная связь не изменилась: отзыв вернул новое значение.
    PDR_CHECK(granted.IsActive());
}

void SecondRevokeIsARefusalAndNotACrash() {
    const pdr::testing::FakeIdGenerator ids;
    const auto start = pdr::testing::FakeClock::DefaultStart();
    const auto once = MakeGranted(ids, start).Revoked(start + 1h);
    PDR_CHECK(once.HasValue());

    const auto twice = once.Value().Revoked(start + 2h);

    PDR_CHECK(!twice.HasValue());
    PDR_CHECK(twice.Failure().Kind() == pdr::core::ErrorKind::kConflict);
    PDR_CHECK(twice.Failure().Code() == "guardianship_already_revoked");
}

void RevokeBeforeGrantIsRefused() {
    const pdr::testing::FakeIdGenerator ids;
    const auto start = pdr::testing::FakeClock::DefaultStart();
    const auto granted = MakeGranted(ids, start);

    const auto refused = granted.Revoked(start - 1h);

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Kind() == pdr::core::ErrorKind::kValidation);
    PDR_CHECK(refused.Failure().Code() == "guardianship_revoked_before_granted");
}

}  // namespace

int main() {
    GrantedGuardianshipIsActive();
    RevokeKeepsTheLinkAndItsDate();
    SecondRevokeIsARefusalAndNotACrash();
    RevokeBeforeGrantIsRefused();
    return pdr::testing::Summary("identity.guardianship");
}
