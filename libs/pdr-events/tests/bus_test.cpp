#include <string>
#include <vector>

#include "events/identity/guardianship_revoked.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using pdr::events::InMemoryBus;
using GuardianshipRevoked = pdr::events::identity::GuardianshipRevoked;
using LessonBooked = pdr::events::scheduling::LessonBooked;

pdr::events::Envelope MakeEnvelope(const pdr::testing::FakeIdGenerator& ids,
                                   const pdr::testing::FakeClock& clock) {
    return pdr::events::Envelope{ids.Next<pdr::core::TenantId>(), clock.Now()};
}

GuardianshipRevoked MakeRevoked(const pdr::testing::FakeIdGenerator& ids,
                                const pdr::testing::FakeClock& clock) {
    return GuardianshipRevoked{
        MakeEnvelope(ids, clock), ids.Next<pdr::core::PersonId>(), ids.Next<pdr::core::PersonId>()};
}

void SubscriberGetsWhatItAskedFor() {
    const pdr::testing::FakeIdGenerator ids;
    const pdr::testing::FakeClock clock;
    InMemoryBus bus;

    std::vector<std::string> seen;
    bus.Subscribe<GuardianshipRevoked>(
        [&seen](const GuardianshipRevoked& event) { seen.push_back(event.student.ToString()); });

    const auto revoked = MakeRevoked(ids, clock);
    bus.Publish(revoked);

    PDR_CHECK(seen.size() == 1);
    PDR_CHECK(seen.front() == revoked.student.ToString());
    PDR_CHECK(bus.Published() == 1);
}

void SubscriberDoesNotGetSomebodyElsesEvent() {
    const pdr::testing::FakeIdGenerator ids;
    const pdr::testing::FakeClock clock;
    InMemoryBus bus;

    int lessons = 0;
    bus.Subscribe<LessonBooked>([&lessons](const LessonBooked&) { ++lessons; });

    bus.Publish(MakeRevoked(ids, clock));

    PDR_CHECK(lessons == 0);
}

void PublisherDoesNotKnowHowManyAreListening() {
    const pdr::testing::FakeIdGenerator ids;
    const pdr::testing::FakeClock clock;
    InMemoryBus bus;

    // Событие без единого подписчика — не ошибка.
    bus.Publish(MakeRevoked(ids, clock));
    PDR_CHECK(bus.Published() == 1);

    int first = 0;
    int second = 0;
    bus.Subscribe<GuardianshipRevoked>([&first](const GuardianshipRevoked&) { ++first; });
    bus.Subscribe<GuardianshipRevoked>([&second](const GuardianshipRevoked&) { ++second; });

    bus.Publish(MakeRevoked(ids, clock));

    // Второй подписчик добавился, не тронув ни строчки у издателя и не заметив
    // первого.
    PDR_CHECK(first == 1);
    PDR_CHECK(second == 1);
    PDR_CHECK(bus.Published() == 2);
}

}  // namespace

int main() {
    SubscriberGetsWhatItAskedFor();
    SubscriberDoesNotGetSomebodyElsesEvent();
    PublisherDoesNotKnowHowManyAreListening();
    return pdr::testing::Summary("events.bus");
}
