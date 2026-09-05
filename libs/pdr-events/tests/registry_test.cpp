#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "events/event.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"

/// @file
/// Реестр событий — это каталог include/events/<контекст>/, по заголовку на тип.
/// Общего заголовка «все события всех контекстов» в реестре нет: включать все
/// типы сразу незачем, а сам факт такого заголовка вернул бы связь между
/// контекстами. Этот файл — единственное место, где они собраны вместе, и
/// собраны затем, чтобы проверить имена.

namespace pdr::events {
namespace {

using GuardianshipRevoked = identity::GuardianshipRevoked;
using LessonBooked = scheduling::LessonBooked;
using LessonCancelled = scheduling::LessonCancelled;

static_assert(Event<GuardianshipRevoked>);
static_assert(Event<LessonBooked>);
static_assert(Event<LessonCancelled>);

TEST(EventRegistry, TypeNamesAreStableAndUnique) {
    const std::set<std::string_view> types{
        GuardianshipRevoked::kType,
        LessonBooked::kType,
        LessonCancelled::kType,
    };

    EXPECT_EQ(types.size(), 3U) << "два события с одним именем типа";
    EXPECT_EQ(GuardianshipRevoked::kType, "identity.guardianship_revoked");
    EXPECT_EQ(LessonBooked::kType, "scheduling.lesson_booked");
    EXPECT_EQ(LessonCancelled::kType, "scheduling.lesson_cancelled");
}

TEST(EventRegistry, TypeNameStartsWithPublishingContext) {
    EXPECT_EQ(GuardianshipRevoked::kType.substr(0, 9), "identity.");
    EXPECT_EQ(LessonBooked::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonCancelled::kType.substr(0, 11), "scheduling.");
}

TEST(EventRegistry, EveryEventIsVersioned) {
    EXPECT_GE(GuardianshipRevoked::kVersion, 1);
    EXPECT_GE(LessonBooked::kVersion, 1);
    EXPECT_GE(LessonCancelled::kVersion, 1);
}

}  // namespace
}  // namespace pdr::events
