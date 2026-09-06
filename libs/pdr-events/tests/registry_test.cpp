#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "events/event.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/notifications/reminder_day_before.hpp"
#include "events/notifications/reminder_hour_before.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"

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
using LessonRescheduled = scheduling::LessonRescheduled;
using ReminderDayBefore = notifications::ReminderDayBefore;
using ReminderHourBefore = notifications::ReminderHourBefore;

static_assert(Event<GuardianshipRevoked>);
static_assert(Event<LessonBooked>);
static_assert(Event<LessonCancelled>);
static_assert(Event<LessonRescheduled>);
static_assert(Event<ReminderDayBefore>);
static_assert(Event<ReminderHourBefore>);

TEST(EventRegistry, TypeNamesAreStableAndUnique) {
    const std::set<std::string_view> types{
        GuardianshipRevoked::kType,
        LessonBooked::kType,
        LessonCancelled::kType,
        LessonRescheduled::kType,
        ReminderDayBefore::kType,
        ReminderHourBefore::kType,
    };

    EXPECT_EQ(types.size(), 6U) << "два события с одним именем типа";
    EXPECT_EQ(GuardianshipRevoked::kType, "identity.guardianship_revoked");
    EXPECT_EQ(LessonBooked::kType, "scheduling.lesson_booked");
    EXPECT_EQ(LessonCancelled::kType, "scheduling.lesson_cancelled");
    EXPECT_EQ(LessonRescheduled::kType, "scheduling.lesson_rescheduled");
    EXPECT_EQ(ReminderDayBefore::kType, "notifications.reminder_day_before");
    EXPECT_EQ(ReminderHourBefore::kType, "notifications.reminder_hour_before");
}

TEST(EventRegistry, TypeNameStartsWithPublishingContext) {
    EXPECT_EQ(GuardianshipRevoked::kType.substr(0, 9), "identity.");
    EXPECT_EQ(LessonBooked::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonCancelled::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonRescheduled::kType.substr(0, 11), "scheduling.");

    /// НАПОМИНАНИЯ ИЗДАЁТ notifications, И ЭТО НЕ ОПИСКА. Что занятие записано,
    /// знает расписание; что о нём напоминают за сутки, а не за двое, — решение
    /// оповещений, и приставка называет того, чьё это решение.
    EXPECT_EQ(ReminderDayBefore::kType.substr(0, 14), "notifications.");
    EXPECT_EQ(ReminderHourBefore::kType.substr(0, 14), "notifications.");
}

TEST(EventRegistry, EveryEventIsVersioned) {
    EXPECT_GE(GuardianshipRevoked::kVersion, 1);
    EXPECT_GE(LessonBooked::kVersion, 1);
    EXPECT_GE(LessonCancelled::kVersion, 1);
    EXPECT_GE(LessonRescheduled::kVersion, 1);
    EXPECT_GE(ReminderDayBefore::kVersion, 1);
    EXPECT_GE(ReminderHourBefore::kVersion, 1);
}

}  // namespace
}  // namespace pdr::events
