#include "scheduling/application/book_lesson.hpp"

#include <algorithm>
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
#include "scheduling/core/overlap.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::Numbered;

constexpr auto kNoBuffer = core::Instant::Duration::zero();

class FakeLessons final : public ports::LessonRepository {
public:
    std::optional<Lesson> Find(const core::TenantId&, const core::LessonId& id) const override {
        for (const auto& lesson : saved_) {
            if (lesson.Id() == id) {
                return lesson;
            }
        }
        return std::nullopt;
    }

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

    /// ФЕЙК ОТКАЗЫВАЕТ ТАМ ЖЕ, ГДЕ ОТКАЖЕТ БАЗА.
    ///
    /// Пересечение у репетитора запрещено ограничением
    /// `scheduling_lesson_no_overlap`. Фейк, который молча принимает такое
    /// занятие, делает unit-прогон зелёным на поведении, которого в проде нет.
    core::Result<void> Save(const Lesson& lesson) override {
        for (const auto& kept : saved_) {
            if (kept.Tutor() == lesson.Tutor() && Overlaps(kept, lesson, kNoBuffer)) {
                return core::Error{core::ErrorKind::kConflict,
                                   "slot_already_taken",
                                   "это время у репетитора уже занято"};
            }
        }
        saved_.push_back(lesson);
        return {};
    }

    std::vector<Lesson> OfTutor(const core::TenantId&,
                                const core::PersonId& tutor,
                                const core::TimeRange& window) const override {
        std::vector<Lesson> found;
        for (const auto& lesson : saved_) {
            if (lesson.Tutor() == tutor && window.Contains(lesson.StartsAt())) {
                found.push_back(lesson);
            }
        }
        return found;
    }

    std::vector<Lesson> OfParticipant(const core::TenantId&,
                                      const core::PersonId& participant,
                                      const core::TimeRange& window) const override {
        std::vector<Lesson> found;
        for (const auto& lesson : saved_) {
            const auto& people = lesson.Participants();
            if (std::find(people.begin(), people.end(), participant) != people.end() &&
                window.Contains(lesson.StartsAt())) {
                found.push_back(lesson);
            }
        }
        return found;
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
        return {tenant_,
                tutor_,
                student_,
                clock_.Now() + 48h,
                60min,
                core::TimeZone::Parse("Europe/Moscow").value()};
    }

    BookLesson Booking() {
        return BookLesson{lessons_, clock_, ids_, bus_};
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
    std::vector<pdr::events::scheduling::LessonBooked> heard;
    bus_.Subscribe<pdr::events::scheduling::LessonBooked>(
        [&heard](const pdr::events::scheduling::LessonBooked& event) { heard.push_back(event); });

    const auto book = Booking();
    const auto request = Request();
    const auto booked = book.Execute(request);

    ASSERT_TRUE(booked.HasValue());
    ASSERT_EQ(lessons_.Saved().size(), 1U);
    EXPECT_TRUE(lessons_.Saved().front().Id() == booked.Value().Id());
    EXPECT_TRUE(lessons_.Saved().front().EndsAt() == request.starts_at + 60min);

    ASSERT_EQ(heard.size(), 1U);
    EXPECT_TRUE(heard.front().lesson == booked.Value().Id());
    EXPECT_TRUE(heard.front().starts_at == request.starts_at);
    EXPECT_TRUE(heard.front().envelope.occurred_at == clock_.Now());
}

TEST_F(BookLessonTest, TakenSlotIsAConflict) {
    const auto book = Booking();

    ASSERT_TRUE(book.Execute(Request()).HasValue());
    const auto again = book.Execute(Request());

    ASSERT_FALSE(again.HasValue());
    EXPECT_EQ(again.Failure().Kind(), core::ErrorKind::kConflict);
    EXPECT_EQ(again.Failure().Code(), "slot_already_taken");
    EXPECT_EQ(bus_.Published(), 1U);
}

TEST_F(BookLessonTest, PastAndEmptyLessonsAreRefused) {
    const auto book = Booking();

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
    const auto book = Booking();

    const auto request = Request();
    clock_.Advance(72h);

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
