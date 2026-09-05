#include "scheduling/application/list_lessons.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "fakes/fake_scheduling.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeLessons;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::MomentBuilder;
using pdr::testing::Numbered;

core::TenantId Tenant() {
    return Numbered<core::TenantId>(1);
}

core::PersonId Tutor() {
    return Numbered<core::PersonId>(10);
}

core::PersonId Student() {
    return Numbered<core::PersonId>(20);
}

core::Instant At(unsigned day, unsigned hour) {
    return MomentBuilder{}.Utc(2026, 3, day).At(hour, 0).Build();
}

core::TimeRange Window(core::Instant from, core::Instant to) {
    return core::TimeRange::Compose(from, to).Value();
}

Lesson ALesson(std::uint64_t number, core::Instant starts_at) {
    return LessonBuilder{}
        .Id(Numbered<core::LessonId>(number))
        .InTenant(Tenant())
        .Between(Tutor(), Student())
        .StartingAt(starts_at)
        .Build();
}

class ListLessonsTest : public ::testing::Test {
protected:
    void Given(std::uint64_t number, core::Instant starts_at) {
        ASSERT_TRUE(lessons_.Save(ALesson(number, starts_at)).HasValue());
    }

    FakeLessons lessons_;
};

TEST_F(ListLessonsTest, TheTutorSideAndTheParticipantSideAreDifferentQuestions) {
    Given(1, At(3, 10));

    const ListLessons showing{lessons_};
    const auto window = Window(At(1, 0), At(31, 0));

    const auto his = showing.Execute({Tenant(), Tutor(), Side::kTutor, window});
    ASSERT_TRUE(his.HasValue());
    EXPECT_EQ(his.Value().size(), 1U);

    const auto hers = showing.Execute({Tenant(), Student(), Side::kParticipant, window});
    ASSERT_TRUE(hers.HasValue());
    EXPECT_EQ(hers.Value().size(), 1U);

    const auto neither = showing.Execute({Tenant(), Student(), Side::kTutor, window});
    ASSERT_TRUE(neither.HasValue());
    EXPECT_TRUE(neither.Value().empty()) << "ученик оказался репетитором своего же занятия";
}

TEST_F(ListLessonsTest, OnlyWhatFallsInTheWindowComesBack) {
    Given(1, At(3, 10));
    Given(2, At(20, 10));

    const ListLessons showing{lessons_};

    const auto found =
        showing.Execute({Tenant(), Tutor(), Side::kTutor, Window(At(1, 0), At(10, 0))});

    ASSERT_TRUE(found.HasValue());
    ASSERT_EQ(found.Value().size(), 1U);
    EXPECT_TRUE(found.Value().front().StartsAt() == At(3, 10));
}

/// ОТРЕЗОК ШИРЕ ГОРИЗОНТА — ОТКАЗ, а не молчаливое усечение: усечённый ответ
/// без предупреждения выглядит как «дальше занятий нет».
TEST_F(ListLessonsTest, AWindowWiderThanTheHorizonIsRefused) {
    const ListLessons showing{lessons_};
    const auto from = At(1, 0);
    const auto over = Window(from, from + kDefaultHorizon + 1h);

    const auto refused = showing.Execute({Tenant(), Tutor(), Side::kTutor, over});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kValidation);
    EXPECT_EQ(refused.Failure().Code(), "schedule_window_over_horizon");
}

TEST_F(ListLessonsTest, AWindowExactlyTheHorizonIsLetThrough) {
    const ListLessons showing{lessons_};
    const auto from = At(1, 0);

    EXPECT_TRUE(
        showing.Execute({Tenant(), Tutor(), Side::kTutor, Window(from, from + kDefaultHorizon)})
            .HasValue());
}

}  // namespace
}  // namespace pdr::scheduling
