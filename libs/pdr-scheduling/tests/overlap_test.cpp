#include "scheduling/core/overlap.hpp"

#include <chrono>

#include <gtest/gtest.h>

#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::MomentBuilder;

constexpr auto kNoBuffer = core::Instant::Duration::zero();

Lesson At(unsigned hour, unsigned minute, Lesson::Duration duration = 60min) {
    return LessonBuilder{}
        .StartingAt(MomentBuilder{}.Utc(2026, 3, 2).At(hour, minute).Build())
        .Lasting(duration)
        .Build();
}

}  // namespace

TEST(Overlaps, LessonsFarApartDoNotTouch) {
    EXPECT_FALSE(Overlaps(At(10, 0), At(14, 0), kNoBuffer));
}

TEST(Overlaps, LessonsSharingAnHourDo) {
    EXPECT_TRUE(Overlaps(At(10, 0), At(10, 30), kNoBuffer));
    EXPECT_TRUE(Overlaps(At(10, 30), At(10, 0), kNoBuffer));
}

TEST(Overlaps, BackToBackLessonsDoNotOverlapWithoutABuffer) {
    EXPECT_FALSE(Overlaps(At(10, 0), At(11, 0), kNoBuffer));
}

/// ПЕРЕСЕЧЕНИЕ РОВНО НА ГРАНИЦЕ БУФЕРА.
///
/// Пять минут перерыва означают, что пяти минут ДОСТАТОЧНО: занятие, начатое
/// ровно через пять минут после конца предыдущего, конфликтом не считается, а
/// начатое через четыре — считается. Граница проверяется с обеих сторон, потому
/// что ошибка здесь всегда на единицу и всегда в одну из них.
TEST(Overlaps, ExactlyTheBufferIsEnoughAndAMinuteLessIsNot) {
    const auto first = At(10, 0);
    const auto buffer = std::chrono::duration_cast<core::Instant::Duration>(5min);

    EXPECT_FALSE(Overlaps(first, At(11, 5), buffer));
    EXPECT_TRUE(Overlaps(first, At(11, 4), buffer));

    EXPECT_FALSE(Overlaps(At(11, 5), first, buffer));
    EXPECT_TRUE(Overlaps(At(11, 4), first, buffer));
}

TEST(Overlaps, TheBufferCountsOnBothSidesOfTheLesson) {
    const auto middle = At(12, 0);
    const auto buffer = std::chrono::duration_cast<core::Instant::Duration>(15min);

    EXPECT_TRUE(Overlaps(middle, At(10, 50), buffer));
    EXPECT_FALSE(Overlaps(middle, At(10, 45), buffer));
    EXPECT_TRUE(Overlaps(middle, At(13, 10), buffer));
    EXPECT_FALSE(Overlaps(middle, At(13, 15), buffer));
}

TEST(Overlaps, ALessonAlwaysOverlapsItself) {
    const auto lesson = At(10, 0);

    EXPECT_TRUE(Overlaps(lesson, lesson, kNoBuffer));
}

TEST(Overlaps, ALongerLessonSwallowsAShorterOne) {
    EXPECT_TRUE(Overlaps(At(10, 0, 180min), At(11, 0, 30min), kNoBuffer));
}

}  // namespace pdr::scheduling
