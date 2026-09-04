#include <chrono>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "scheduling/core/lesson.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::Numbered;

/// Разрешённые переходы, написанные ВТОРОЙ РАЗ и в другой форме.
///
/// Таблица в lesson.cpp — это код; здесь — то, чего от него ждут. Сверять код с
/// самим собой бессмысленно: тест, читающий ту же таблицу, зелен при любой её
/// правке, включая ошибочную.
const std::set<std::pair<LessonState, LessonEvent>> kAllowed{
    {LessonState::kPlanned, LessonEvent::kConfirm},
    {LessonState::kPlanned, LessonEvent::kHold},
    {LessonState::kPlanned, LessonEvent::kCancel},
    {LessonState::kPlanned, LessonEvent::kMarkNoShow},
    {LessonState::kConfirmed, LessonEvent::kHold},
    {LessonState::kConfirmed, LessonEvent::kCancel},
    {LessonState::kConfirmed, LessonEvent::kMarkNoShow},
};

}  // namespace

/// КАЖДЫЙ НЕДОПУСТИМЫЙ ПЕРЕХОД ОТКЛОНЁН.
///
/// Обходятся ВСЕ пары состояние×событие, а не выбранные руками: пропущенная
/// пара — это ровно тот переход, который однажды окажется разрешённым по
/// недосмотру. Границы списков стерегут `static_assert` в заголовке: заведённое
/// состояние без строки в `kEveryLessonState` не соберётся.
TEST(LessonTransitions, EveryPairIsEitherAllowedOnPurposeOrRefused) {
    std::size_t allowed = 0;
    std::size_t refused = 0;

    for (const auto state : kEveryLessonState) {
        for (const auto event : kEveryLessonEvent) {
            const auto moved = Transition(state, event);
            const bool expected = kAllowed.count({state, event}) != 0;

            EXPECT_EQ(moved.HasValue(), expected)
                << "переход «" << Name(state) << "» + «" << Name(event) << "»";
            if (moved.HasValue()) {
                ++allowed;
            } else {
                ++refused;
                EXPECT_EQ(moved.Failure().Code(), "lesson_transition_not_allowed");
                EXPECT_EQ(moved.Failure().Kind(), core::ErrorKind::kConflict);
            }
        }
    }

    EXPECT_EQ(allowed, kAllowed.size());
    EXPECT_EQ(refused, kEveryLessonState.size() * kEveryLessonEvent.size() - kAllowed.size());
}

TEST(LessonTransitions, AllowedOnesLandWhereTheyShould) {
    EXPECT_EQ(Transition(LessonState::kPlanned, LessonEvent::kConfirm).Value(),
              LessonState::kConfirmed);
    EXPECT_EQ(Transition(LessonState::kPlanned, LessonEvent::kHold).Value(), LessonState::kHeld);
    EXPECT_EQ(Transition(LessonState::kPlanned, LessonEvent::kCancel).Value(),
              LessonState::kCancelled);
    EXPECT_EQ(Transition(LessonState::kPlanned, LessonEvent::kMarkNoShow).Value(),
              LessonState::kNoShow);
    EXPECT_EQ(Transition(LessonState::kConfirmed, LessonEvent::kHold).Value(), LessonState::kHeld);
    EXPECT_EQ(Transition(LessonState::kConfirmed, LessonEvent::kCancel).Value(),
              LessonState::kCancelled);
    EXPECT_EQ(Transition(LessonState::kConfirmed, LessonEvent::kMarkNoShow).Value(),
              LessonState::kNoShow);
}

/// Конечные состояния конечны. Отдельным тестом, а не следствием обхода: это
/// продуктовое обещание, и читаться оно должно словами.
TEST(LessonTransitions, WhatIsOverStaysOver) {
    for (const auto state : {LessonState::kHeld, LessonState::kCancelled, LessonState::kNoShow}) {
        for (const auto event : kEveryLessonEvent) {
            EXPECT_FALSE(Transition(state, event).HasValue())
                << "из «" << Name(state) << "» ушли по «" << Name(event) << "»";
        }
    }
}

TEST(LessonState, ANewLessonIsPlanned) {
    EXPECT_EQ(LessonBuilder{}.Build().State(), LessonState::kPlanned);
}

TEST(LessonState, TheLessonAfterAnEventIsANewValue) {
    const auto planned = LessonBuilder{}.Build();
    const auto confirmed = planned.After(LessonEvent::kConfirm);

    ASSERT_TRUE(confirmed.HasValue());
    EXPECT_EQ(confirmed.Value().State(), LessonState::kConfirmed);
    EXPECT_EQ(planned.State(), LessonState::kPlanned);
    EXPECT_EQ(confirmed.Value().Id(), planned.Id());
    EXPECT_EQ(confirmed.Value().StartsAt(), planned.StartsAt());
}

TEST(LessonState, ARefusedEventLeavesNoLesson) {
    const auto held = LessonBuilder{}.Build().After(LessonEvent::kHold).Value();
    const auto cancelled = held.After(LessonEvent::kCancel);

    ASSERT_FALSE(cancelled.HasValue());
    EXPECT_EQ(cancelled.Failure().Code(), "lesson_transition_not_allowed");
}

TEST(LessonParticipants, AreAVectorAndTodayHoldExactlyOne) {
    const auto lesson = LessonBuilder{}.Build();

    ASSERT_EQ(lesson.Participants().size(), Lesson::kParticipantsForNow);
}

TEST(LessonParticipants, RefuseAnEmptyOrCrowdedLesson) {
    const auto tutor = Numbered<core::PersonId>(10);
    const auto tenant = Numbered<core::TenantId>(1);
    const auto id = Numbered<core::LessonId>(100);
    const auto starts = pdr::testing::MomentBuilder{}.Utc(2026, 3, 2).At(18, 0).Build();
    const auto now = starts - 24h;

    const auto empty = Lesson::Schedule(id, tenant, tutor, {}, starts, 60min, now);
    ASSERT_FALSE(empty.HasValue());
    EXPECT_EQ(empty.Failure().Code(), "lesson_participants_not_one");

    const auto crowded =
        Lesson::Schedule(id,
                         tenant,
                         tutor,
                         {Numbered<core::PersonId>(20), Numbered<core::PersonId>(21)},
                         starts,
                         60min,
                         now);
    ASSERT_FALSE(crowded.HasValue());
    EXPECT_EQ(crowded.Failure().Code(), "lesson_participants_not_one");
}

TEST(LessonParticipants, RefuseTheTutorAmongThem) {
    const auto tutor = Numbered<core::PersonId>(10);
    const auto starts = pdr::testing::MomentBuilder{}.Utc(2026, 3, 2).At(18, 0).Build();

    const auto wrong = Lesson::Schedule(Numbered<core::LessonId>(100),
                                        Numbered<core::TenantId>(1),
                                        tutor,
                                        {tutor},
                                        starts,
                                        60min,
                                        starts - 24h);

    ASSERT_FALSE(wrong.HasValue());
    EXPECT_EQ(wrong.Failure().Code(), "lesson_tutor_among_participants");
}

}  // namespace pdr::scheduling
