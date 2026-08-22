#include "scheduling/application/book_lesson.hpp"

#include <chrono>
#include <optional>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "events/in_memory_bus.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "identity/contract.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::Numbered;

/// Дубль чужого контракта. Обратите внимание: тест знает об identity ровно
/// столько же, сколько сам модуль, — один заголовок.
class FakeIdentity final : public identity::Contract {
public:
    explicit FakeIdentity(bool allowed) noexcept : allowed_{allowed} {}

    bool MayActFor(const core::TenantId&,
                   const core::PersonId&,
                   const core::PersonId&) const override {
        return allowed_;
    }

private:
    bool allowed_;
};

class FakeLessons final : public ports::LessonRepository {
public:
    std::optional<Lesson> FindAtSlot(const core::TenantId&,
                                     const core::PersonId&,
                                     core::Instant starts_at) const override {
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

class BookLessonTest : public ::testing::Test {
protected:
    BookLesson::Request Request() const {
        return {tenant_, student_, tutor_, student_, clock_.Now() + 48h, 60min};
    }

    BookLesson Booking(const identity::Contract& identity) {
        return BookLesson{lessons_, identity, clock_, ids_, bus_};
    }

    pdr::testing::FakeIdGenerator ids_;
    pdr::testing::FakeClock clock_;
    pdr::events::InMemoryBus bus_;
    FakeLessons lessons_;

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
    core::PersonId tutor_{Numbered<core::PersonId>(10)};
    core::PersonId student_{Numbered<core::PersonId>(20)};
};

TEST_F(BookLessonTest, BookingSavesPublishesAndReturnsIdentifier) {
    const FakeIdentity identity{true};
    std::vector<pdr::events::scheduling::LessonBooked> heard;
    bus_.Subscribe<pdr::events::scheduling::LessonBooked>(
        [&heard](const pdr::events::scheduling::LessonBooked& event) { heard.push_back(event); });

    const auto book = Booking(identity);
    const auto request = Request();
    const auto booked = book.Execute(request);

    ASSERT_TRUE(booked.HasValue());
    ASSERT_EQ(lessons_.Saved().size(), 1U);
    EXPECT_TRUE(lessons_.Saved().front().Id() == booked.Value());
    EXPECT_TRUE(lessons_.Saved().front().EndsAt() == request.starts_at + 60min);

    ASSERT_EQ(heard.size(), 1U);
    EXPECT_TRUE(heard.front().lesson == booked.Value());
    EXPECT_TRUE(heard.front().starts_at == request.starts_at);
    EXPECT_TRUE(heard.front().envelope.occurred_at == clock_.Now());
}

TEST_F(BookLessonTest, StrangerCannotBookForAStudent) {
    const FakeIdentity identity{false};

    const auto refused = Booking(identity).Execute(Request());

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kForbidden);
    EXPECT_EQ(refused.Failure().Code(), "not_allowed_to_act_for_student");
    EXPECT_TRUE(lessons_.Saved().empty());
    EXPECT_EQ(bus_.Published(), 0U);
}

TEST_F(BookLessonTest, TakenSlotIsAConflict) {
    const FakeIdentity identity{true};
    const auto book = Booking(identity);

    ASSERT_TRUE(book.Execute(Request()).HasValue());
    const auto again = book.Execute(Request());

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Kind(), core::ErrorKind::kConflict);
    EXPECT_EQ(again.Failure().Code(), "slot_already_taken");
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(BookLessonTest, PastAndEmptyLessonsAreRefused) {
    const FakeIdentity identity{true};
    const auto book = Booking(identity);

    auto past = Request();
    past.starts_at = clock_.Now() - 1h;
    const auto refused_past = book.Execute(past);
    ASSERT_FALSE(refused_past.HasValue());
    EXPECT_EQ(refused_past.Failure().Code(), "lesson_starts_in_past");

    auto empty = Request();
    empty.duration = 0min;
    const auto refused_empty = book.Execute(empty);
    ASSERT_FALSE(refused_empty.HasValue());
    EXPECT_EQ(refused_empty.Failure().Code(), "lesson_duration_not_positive");

    EXPECT_EQ(bus_.Published(), 0U);
}

TEST_F(BookLessonTest, ClockMovesAndTheSameSlotBecomesThePast) {
    const FakeIdentity identity{true};
    const auto book = Booking(identity);

    const auto request = Request();
    clock_.Advance(72h);  // мгновенно, без единого sleep

    const auto refused = book.Execute(request);
    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "lesson_starts_in_past");
}

/// Домен занятия отдельно от сценария: билдер собирает занятие, а правило
/// «конец = начало плюс длительность» проверяется без порта и без шины.
TEST(Lesson, EndsWhereItsDurationEnds) {
    const auto lesson = LessonBuilder{}.Lasting(90min).Build();

    EXPECT_TRUE(lesson.EndsAt() == lesson.StartsAt() + 90min);
    EXPECT_TRUE(lesson.LessonDuration() == 90min);
}

TEST(Lesson, CannotBeScheduledInThePast) {
    EXPECT_THROW(LessonBuilder{}
                     .AsOf(pdr::testing::MomentBuilder{}.Utc(2026, 3, 3).At(0, 0).Build())
                     .Build(),
                 std::logic_error);
}

}  // namespace
}  // namespace pdr::scheduling
