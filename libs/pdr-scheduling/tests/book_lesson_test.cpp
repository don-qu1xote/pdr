#include "scheduling/application/book_lesson.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "identity/contract.hpp"
#include "testing/check.hpp"
#include "testing/fake_clock.hpp"
#include "testing/fake_id_generator.hpp"

namespace {

using namespace std::chrono_literals;
using pdr::scheduling::BookLesson;
using pdr::scheduling::Lesson;

/// Дубль чужого контракта. Обратите внимание: тест знает об identity ровно
/// столько же, сколько сам модуль, — один заголовок.
class FakeIdentity final : public pdr::identity::Contract {
public:
    explicit FakeIdentity(bool allowed) noexcept : allowed_{allowed} {}

    bool MayActFor(const pdr::core::TenantId&,
                   const pdr::core::PersonId&,
                   const pdr::core::PersonId&) const override {
        return allowed_;
    }

private:
    bool allowed_;
};

class FakeLessons final : public pdr::scheduling::ports::LessonRepository {
public:
    std::optional<Lesson> FindAtSlot(const pdr::core::TenantId&,
                                     const pdr::core::PersonId&,
                                     pdr::core::Instant starts_at) const override {
        for (const auto& lesson : saved_) {
            if (lesson.StartsAt() == starts_at) {
                return lesson;
            }
        }
        return std::nullopt;
    }

    void Save(const Lesson& lesson) override {
        saved_.push_back(lesson);
    }

    const std::vector<Lesson>& Saved() const noexcept {
        return saved_;
    }

private:
    std::vector<Lesson> saved_;
};

struct Fixture final {
    pdr::testing::FakeIdGenerator ids;
    pdr::testing::FakeClock clock;
    pdr::events::InMemoryBus bus;
    FakeLessons lessons;

    pdr::core::TenantId tenant{ids.Next<pdr::core::TenantId>()};
    pdr::core::PersonId tutor{ids.Next<pdr::core::PersonId>()};
    pdr::core::PersonId student{ids.Next<pdr::core::PersonId>()};

    BookLesson::Request Request() const {
        return {tenant, student, tutor, student, clock.Now() + 48h, 60min};
    }
};

void BookingSavesPublishesAndReturnsIdentifier() {
    Fixture fixture;
    const FakeIdentity identity{true};
    std::vector<pdr::events::scheduling::LessonBooked> heard;
    fixture.bus.Subscribe<pdr::events::scheduling::LessonBooked>(
        [&heard](const pdr::events::scheduling::LessonBooked& event) { heard.push_back(event); });

    const BookLesson book{fixture.lessons, identity, fixture.clock, fixture.ids, fixture.bus};
    const auto request = fixture.Request();
    const auto booked = book.Execute(request);

    PDR_CHECK(booked.HasValue());
    PDR_CHECK(fixture.lessons.Saved().size() == 1);
    PDR_CHECK(fixture.lessons.Saved().front().Id() == booked.Value());
    PDR_CHECK(fixture.lessons.Saved().front().EndsAt() == request.starts_at + 60min);

    PDR_CHECK(heard.size() == 1);
    PDR_CHECK(heard.front().lesson == booked.Value());
    PDR_CHECK(heard.front().starts_at == request.starts_at);
    PDR_CHECK(heard.front().envelope.occurred_at == fixture.clock.Now());
}

void StrangerCannotBookForAStudent() {
    Fixture fixture;
    const FakeIdentity identity{false};

    const BookLesson book{fixture.lessons, identity, fixture.clock, fixture.ids, fixture.bus};
    const auto refused = book.Execute(fixture.Request());

    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Kind() == pdr::core::ErrorKind::kForbidden);
    PDR_CHECK(refused.Failure().Code() == "not_allowed_to_act_for_student");
    PDR_CHECK(fixture.lessons.Saved().empty());
    PDR_CHECK(fixture.bus.Published() == 0);
}

void TakenSlotIsAConflict() {
    Fixture fixture;
    const FakeIdentity identity{true};
    const BookLesson book{fixture.lessons, identity, fixture.clock, fixture.ids, fixture.bus};

    PDR_CHECK(book.Execute(fixture.Request()).HasValue());
    const auto again = book.Execute(fixture.Request());

    PDR_CHECK(!again.HasValue());
    PDR_CHECK(again.Failure().Kind() == pdr::core::ErrorKind::kConflict);
    PDR_CHECK(again.Failure().Code() == "slot_already_taken");
    PDR_CHECK(fixture.bus.Published() == 1);
}

void PastAndEmptyLessonsAreRefused() {
    Fixture fixture;
    const FakeIdentity identity{true};
    const BookLesson book{fixture.lessons, identity, fixture.clock, fixture.ids, fixture.bus};

    auto past = fixture.Request();
    past.starts_at = fixture.clock.Now() - 1h;
    const auto refused_past = book.Execute(past);
    PDR_CHECK(!refused_past.HasValue());
    PDR_CHECK(refused_past.Failure().Code() == "lesson_starts_in_past");

    auto empty = fixture.Request();
    empty.duration = 0min;
    const auto refused_empty = book.Execute(empty);
    PDR_CHECK(!refused_empty.HasValue());
    PDR_CHECK(refused_empty.Failure().Code() == "lesson_duration_not_positive");

    PDR_CHECK(fixture.bus.Published() == 0);
}

void ClockMovesAndTheSameSlotBecomesThePast() {
    Fixture fixture;
    const FakeIdentity identity{true};
    const BookLesson book{fixture.lessons, identity, fixture.clock, fixture.ids, fixture.bus};

    const auto request = fixture.Request();
    fixture.clock.Advance(72h);  // мгновенно, без единого sleep

    const auto refused = book.Execute(request);
    PDR_CHECK(!refused.HasValue());
    PDR_CHECK(refused.Failure().Code() == "lesson_starts_in_past");
}

}  // namespace

int main() {
    BookingSavesPublishesAndReturnsIdentifier();
    StrangerCannotBookForAStudent();
    TakenSlotIsAConflict();
    PastAndEmptyLessonsAreRefused();
    ClockMovesAndTheSameSlotBecomesThePast();
    return pdr::testing::Summary("scheduling.book_lesson");
}
