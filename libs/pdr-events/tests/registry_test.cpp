#include <set>
#include <string_view>

#include <gtest/gtest.h>

#include "events/event.hpp"
#include "events/identity/guardianship_revoked.hpp"
#include "events/notifications/reminder_before_return.hpp"
#include "events/notifications/reminder_day_before.hpp"
#include "events/notifications/reminder_hour_before.hpp"
#include "events/scheduling/lesson_booked.hpp"
#include "events/scheduling/lesson_cancelled.hpp"
#include "events/scheduling/lesson_participant_withdrew.hpp"
#include "events/scheduling/lesson_rescheduled.hpp"
#include "events/scheduling/time_off_applied.hpp"
#include "events/scheduling/time_off_declared.hpp"

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
using LessonParticipantWithdrew = scheduling::LessonParticipantWithdrew;
using LessonRescheduled = scheduling::LessonRescheduled;
using ReminderBeforeReturn = notifications::ReminderBeforeReturn;
using ReminderDayBefore = notifications::ReminderDayBefore;
using ReminderHourBefore = notifications::ReminderHourBefore;
using TimeOffApplied = scheduling::TimeOffApplied;
using TimeOffDeclared = scheduling::TimeOffDeclared;

static_assert(Event<GuardianshipRevoked>);
static_assert(Event<LessonBooked>);
static_assert(Event<LessonCancelled>);
static_assert(Event<LessonParticipantWithdrew>);
static_assert(Event<LessonRescheduled>);
static_assert(Event<ReminderBeforeReturn>);
static_assert(Event<ReminderDayBefore>);
static_assert(Event<ReminderHourBefore>);
static_assert(Event<TimeOffApplied>);
static_assert(Event<TimeOffDeclared>);

TEST(EventRegistry, TypeNamesAreStableAndUnique) {
    const std::set<std::string_view> types{
        GuardianshipRevoked::kType,
        LessonBooked::kType,
        LessonCancelled::kType,
        LessonParticipantWithdrew::kType,
        LessonRescheduled::kType,
        ReminderBeforeReturn::kType,
        ReminderDayBefore::kType,
        ReminderHourBefore::kType,
        TimeOffApplied::kType,
        TimeOffDeclared::kType,
    };

    EXPECT_EQ(types.size(), 10U) << "два события с одним именем типа";
    EXPECT_EQ(GuardianshipRevoked::kType, "identity.guardianship_revoked");
    EXPECT_EQ(LessonBooked::kType, "scheduling.lesson_booked");
    EXPECT_EQ(LessonCancelled::kType, "scheduling.lesson_cancelled");
    EXPECT_EQ(LessonRescheduled::kType, "scheduling.lesson_rescheduled");

    /// ВЫХОД УЧАСТНИКА — СВОЁ ИМЯ, а не поле внутри отмены: подписчик,
    /// различающий их полем, однажды забудет проверить поле (ADR-0023).
    EXPECT_EQ(LessonParticipantWithdrew::kType, "scheduling.lesson_participant_withdrew");
    EXPECT_NE(LessonParticipantWithdrew::kType, LessonCancelled::kType);
    EXPECT_EQ(ReminderDayBefore::kType, "notifications.reminder_day_before");
    EXPECT_EQ(ReminderHourBefore::kType, "notifications.reminder_hour_before");
    EXPECT_EQ(ReminderBeforeReturn::kType, "notifications.reminder_before_return");

    /// ЗАВЕДЕНИЕ ПЕРЕРЫВА И РЕШЕНИЕ ПО НЕМУ — ДВА СОБЫТИЯ, А НЕ ОДНО С ПОЛЕМ.
    /// Они происходят в разное время, и подписчики у них разные: отрезок нужен
    /// биллингу сразу, список получателей — оповещениям и только после решения.
    EXPECT_EQ(TimeOffDeclared::kType, "scheduling.time_off_declared");
    EXPECT_EQ(TimeOffApplied::kType, "scheduling.time_off_applied");
    EXPECT_NE(TimeOffDeclared::kType, TimeOffApplied::kType);
}

TEST(EventRegistry, TypeNameStartsWithPublishingContext) {
    EXPECT_EQ(GuardianshipRevoked::kType.substr(0, 9), "identity.");
    EXPECT_EQ(LessonBooked::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonCancelled::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonRescheduled::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(LessonParticipantWithdrew::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(TimeOffDeclared::kType.substr(0, 11), "scheduling.");
    EXPECT_EQ(TimeOffApplied::kType.substr(0, 11), "scheduling.");

    /// НАПОМИНАНИЯ ИЗДАЁТ notifications, И ЭТО НЕ ОПИСКА. Что занятие записано,
    /// знает расписание; что о нём напоминают за сутки, а не за двое, — решение
    /// оповещений, и приставка называет того, чьё это решение.
    EXPECT_EQ(ReminderDayBefore::kType.substr(0, 14), "notifications.");
    EXPECT_EQ(ReminderHourBefore::kType.substr(0, 14), "notifications.");
    EXPECT_EQ(ReminderBeforeReturn::kType.substr(0, 14), "notifications.");
}

TEST(EventRegistry, EveryEventIsVersioned) {
    EXPECT_GE(GuardianshipRevoked::kVersion, 1);
    EXPECT_GE(LessonBooked::kVersion, 1);
    EXPECT_GE(LessonCancelled::kVersion, 1);
    EXPECT_GE(LessonParticipantWithdrew::kVersion, 1);
    EXPECT_GE(LessonRescheduled::kVersion, 1);
    EXPECT_GE(ReminderBeforeReturn::kVersion, 1);
    EXPECT_GE(ReminderDayBefore::kVersion, 1);
    EXPECT_GE(ReminderHourBefore::kVersion, 1);
    EXPECT_GE(TimeOffApplied::kVersion, 1);
    EXPECT_GE(TimeOffDeclared::kVersion, 1);
}

}  // namespace
}  // namespace pdr::events
