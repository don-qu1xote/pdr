#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "fakes/fake_clock.hpp"

namespace pdr::events {
namespace {

using GuardianshipRevoked = identity::GuardianshipRevoked;
using LessonBooked = scheduling::LessonBooked;
using pdr::testing::Numbered;

class BusTest : public ::testing::Test {
protected:
    GuardianshipRevoked Revoked(unsigned char student) const {
        return GuardianshipRevoked{Envelope{Numbered<core::TenantId>(1), clock_.Now()},
                                   Numbered<core::PersonId>(10),
                                   Numbered<core::PersonId>(student)};
    }

    pdr::testing::FakeClock clock_;
    InMemoryBus bus_;
};

TEST_F(BusTest, SubscriberGetsWhatItAskedFor) {
    std::vector<std::string> seen;
    bus_.Subscribe<GuardianshipRevoked>(
        [&seen](const GuardianshipRevoked& event) { seen.push_back(event.student.ToString()); });

    const auto revoked = Revoked(20);
    bus_.Publish(revoked);

    ASSERT_EQ(seen.size(), 1U);
    EXPECT_EQ(seen.front(), revoked.student.ToString());
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(BusTest, SubscriberDoesNotGetSomebodyElsesEvent) {
    int lessons = 0;
    bus_.Subscribe<LessonBooked>([&lessons](const LessonBooked&) { ++lessons; });

    bus_.Publish(Revoked(20));

    EXPECT_EQ(lessons, 0);
}

TEST_F(BusTest, PublisherDoesNotKnowHowManyAreListening) {
    // Событие без единого подписчика — не ошибка.
    bus_.Publish(Revoked(20));
    EXPECT_EQ(bus_.Published(), 1U);

    int first = 0;
    int second = 0;
    bus_.Subscribe<GuardianshipRevoked>([&first](const GuardianshipRevoked&) { ++first; });
    bus_.Subscribe<GuardianshipRevoked>([&second](const GuardianshipRevoked&) { ++second; });

    bus_.Publish(Revoked(21));

    // Второй подписчик добавился, не тронув ни строчки у издателя и не заметив
    // первого.
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 1);
    EXPECT_EQ(bus_.Published(), 2U);
}

}  // namespace
}  // namespace pdr::events
