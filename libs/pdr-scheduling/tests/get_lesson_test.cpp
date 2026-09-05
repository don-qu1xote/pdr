#include "scheduling/application/get_lesson.hpp"

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "fakes/fake_scheduling.hpp"

namespace pdr::scheduling {
namespace {

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

core::PersonId Stranger() {
    return Numbered<core::PersonId>(30);
}

core::LessonId Which() {
    return Numbered<core::LessonId>(100);
}

class GetLessonTest : public ::testing::Test {
protected:
    GetLessonTest() {
        const auto lesson = LessonBuilder{}
                                .Id(Which())
                                .InTenant(Tenant())
                                .Between(Tutor(), Student())
                                .StartingAt(MomentBuilder{}.Utc(2026, 3, 3).At(10, 0).Build())
                                .Build();
        EXPECT_TRUE(lessons_.Save(lesson).HasValue());
    }

    FakeLessons lessons_;
};

TEST_F(GetLessonTest, BothSidesSeeTheirOwnLesson) {
    const GetLesson showing{lessons_};

    const auto his = showing.Execute({Tenant(), Tutor(), Side::kTutor, Which()});
    ASSERT_TRUE(his.HasValue());
    EXPECT_TRUE(his.Value().Id() == Which());

    const auto hers = showing.Execute({Tenant(), Student(), Side::kParticipant, Which()});
    ASSERT_TRUE(hers.HasValue());
    EXPECT_TRUE(hers.Value().Id() == Which());
}

/// ЧУЖОЕ ЗАНЯТИЕ НЕ НАХОДИТСЯ ТАК ЖЕ, КАК НЕСУЩЕСТВУЮЩЕЕ: иначе по коду ответа
/// перебираются чужие занятия.
TEST_F(GetLessonTest, ALessonOutsideTheNamedScheduleIsNotFound) {
    const GetLesson showing{lessons_};

    const auto refused = showing.Execute({Tenant(), Stranger(), Side::kParticipant, Which()});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kNotFound);
    EXPECT_EQ(refused.Failure().Code(), "lesson_not_found");
}

TEST_F(GetLessonTest, TheSameLessonAskedFromTheWrongSideIsNotFound) {
    const GetLesson showing{lessons_};

    const auto refused = showing.Execute({Tenant(), Tutor(), Side::kParticipant, Which()});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "lesson_not_found");
}

TEST_F(GetLessonTest, WhatWasNeverSavedIsNotFound) {
    const GetLesson showing{lessons_};

    const auto refused =
        showing.Execute({Tenant(), Tutor(), Side::kTutor, Numbered<core::LessonId>(999)});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "lesson_not_found");
}

}  // namespace
}  // namespace pdr::scheduling
