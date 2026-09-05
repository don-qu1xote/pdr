#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "scheduling/core/availability.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/recurrence.hpp"
#include "scheduling_ground.hpp"

/// @file
/// Contract-набор портов расписания: ОДИН набор проверок, инстанцируемый для
/// каждой реализации порта.
///
/// Зачем он есть — то же, что у набора хранилища (PDR-TST-01): unit-тесты стоят
/// на фейках, и если фейк ведёт себя не так, как настоящий адаптер, зелёный
/// unit-прогон не значит ничего. Два скопированных файла эту работу не делают:
/// они расходятся в первый же день, когда правку внесли в один.
///
/// Лежит в модуле scheduling, а не в платформенной оснастке: набор знает
/// доменные типы контекста, а `libs/pdr-testing` не имеет права зависеть от
/// контекста (docs/testing.md).
///
/// Набор разворачивается и в UTEST-макросы: цель, которая линкует
/// `userver::utest`, переопределяет четыре макроса ниже до включения файла.
/// По умолчанию — обычный gtest, без userver и без базы.

#ifndef PDR_CONTRACT_SUITE_P
#define PDR_CONTRACT_SUITE_P(suite) TYPED_TEST_SUITE_P(suite)
#endif

#ifndef PDR_CONTRACT_TEST_P
#define PDR_CONTRACT_TEST_P(suite, name) TYPED_TEST_P(suite, name)
#endif

#ifndef PDR_CONTRACT_REGISTER_P
#define PDR_CONTRACT_REGISTER_P(suite, ...) REGISTER_TYPED_TEST_SUITE_P(suite, __VA_ARGS__)
#endif

#ifndef PDR_CONTRACT_INSTANTIATE_P
#define PDR_CONTRACT_INSTANTIATE_P(prefix, suite, types) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, suite, types)
#endif

namespace pdr::scheduling::testing {

template<class World>
class LessonRepositoryContract : public ::testing::Test {
protected:
    World world_;
};

PDR_CONTRACT_SUITE_P(LessonRepositoryContract);

PDR_CONTRACT_TEST_P(LessonRepositoryContract, SavedLessonIsFoundAtItsSlot) {
    auto& lessons = this->world_.Lessons();
    const auto starts = ContractGround::Utc(2026, 3, 2, 15);
    const auto lesson = ContractGround::ALesson(this->world_.NextLessonId(), starts);

    ASSERT_TRUE(lessons.Save(lesson).HasValue());

    const auto found =
        lessons.FindAtSlot(ContractGround::Tenant(), ContractGround::Tutor(), starts);
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->Id() == lesson.Id());
    EXPECT_TRUE(found->StartsAt() == starts);
    EXPECT_TRUE(found->EndsAt() == lesson.EndsAt());
    EXPECT_EQ(found->State(), LessonState::kPlanned);
    ASSERT_EQ(found->Participants().size(), 1U);
    EXPECT_TRUE(found->Participants().front() == ContractGround::Student());
}

PDR_CONTRACT_TEST_P(LessonRepositoryContract, AnEmptySlotHoldsNothing) {
    auto& lessons = this->world_.Lessons();

    EXPECT_FALSE(lessons
                     .FindAtSlot(ContractGround::Tenant(),
                                 ContractGround::Tutor(),
                                 ContractGround::Utc(2026, 3, 2, 9))
                     .has_value());
}

PDR_CONTRACT_TEST_P(LessonRepositoryContract, TheTutorSeesHisLessonsInTheWindowAndOnlyThem) {
    auto& lessons = this->world_.Lessons();
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 2, 15)))
                    .HasValue());
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 9, 15)))
                    .HasValue());
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 5, 4, 15)))
                    .HasValue());

    const auto march = lessons.OfTutor(ContractGround::Tenant(),
                                       ContractGround::Tutor(),
                                       ContractGround::Window(ContractGround::Utc(2026, 3, 1, 0),
                                                              ContractGround::Utc(2026, 4, 1, 0)));

    ASSERT_EQ(march.size(), 2U);
    EXPECT_TRUE(march.front().StartsAt() == ContractGround::Utc(2026, 3, 2, 15));
    EXPECT_TRUE(march.back().StartsAt() == ContractGround::Utc(2026, 3, 9, 15));
}

PDR_CONTRACT_TEST_P(LessonRepositoryContract, TheParticipantSeesTheSameLessons) {
    auto& lessons = this->world_.Lessons();
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 2, 15)))
                    .HasValue());

    const auto seen =
        lessons.OfParticipant(ContractGround::Tenant(),
                              ContractGround::Student(),
                              ContractGround::Window(ContractGround::Utc(2026, 3, 1, 0),
                                                     ContractGround::Utc(2026, 4, 1, 0)));

    ASSERT_EQ(seen.size(), 1U);
    EXPECT_TRUE(seen.front().StartsAt() == ContractGround::Utc(2026, 3, 2, 15));
}

/// ГЛАВНОЕ УТВЕРЖДЕНИЕ НАБОРА: ПЕРЕСЕЧЕНИЕ ОТКЛОНЯЕТСЯ ОБЕИМИ РЕАЛИЗАЦИЯМИ.
///
/// У базы это ограничение `scheduling_lesson_no_overlap`, у фейка — та же
/// проверка руками. Фейк, который молча принимает пересечение, делает
/// unit-прогон зелёным на поведении, которого в проде нет, и «слот занят»
/// впервые случается у живого репетитора.
PDR_CONTRACT_TEST_P(LessonRepositoryContract, AnOverlappingLessonIsRefused) {
    auto& lessons = this->world_.Lessons();
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 2, 15)))
                    .HasValue());

    const auto refused = lessons.Save(
        ContractGround::ALesson(this->world_.NextLessonId(), ContractGround::Utc(2026, 3, 2, 15)));

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "slot_already_taken");
    EXPECT_EQ(refused.Failure().Kind(), core::ErrorKind::kConflict);
}

/// Оговорка: встык — не пересечение. Без неё предыдущее утверждение было бы
/// зелёным и в реализации, отклоняющей вообще всё.
PDR_CONTRACT_TEST_P(LessonRepositoryContract, ABackToBackLessonIsAccepted) {
    auto& lessons = this->world_.Lessons();
    ASSERT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 2, 15)))
                    .HasValue());

    EXPECT_TRUE(lessons
                    .Save(ContractGround::ALesson(this->world_.NextLessonId(),
                                                  ContractGround::Utc(2026, 3, 2, 16)))
                    .HasValue());
}

PDR_CONTRACT_TEST_P(LessonRepositoryContract, SavedLessonIsFoundByItsId) {
    auto& lessons = this->world_.Lessons();
    const auto starts = ContractGround::Utc(2026, 3, 2, 15);
    const auto lesson = ContractGround::ALesson(this->world_.NextLessonId(), starts);
    ASSERT_TRUE(lessons.Save(lesson).HasValue());

    const auto found = lessons.Find(ContractGround::Tenant(), lesson.Id());
    ASSERT_TRUE(found.has_value());
    EXPECT_TRUE(found->Id() == lesson.Id());
    EXPECT_TRUE(found->Tutor() == ContractGround::Tutor());
    EXPECT_TRUE(found->StartsAt() == starts);
    ASSERT_EQ(found->Participants().size(), 1U);
    EXPECT_TRUE(found->Participants().front() == ContractGround::Student());
}

PDR_CONTRACT_TEST_P(LessonRepositoryContract, AnUnknownIdHoldsNothing) {
    auto& lessons = this->world_.Lessons();

    EXPECT_FALSE(lessons.Find(ContractGround::Tenant(), this->world_.NextLessonId()).has_value());
}

PDR_CONTRACT_REGISTER_P(LessonRepositoryContract,
                        SavedLessonIsFoundAtItsSlot,
                        SavedLessonIsFoundByItsId,
                        AnUnknownIdHoldsNothing,
                        AnEmptySlotHoldsNothing,
                        TheTutorSeesHisLessonsInTheWindowAndOnlyThem,
                        TheParticipantSeesTheSameLessons,
                        AnOverlappingLessonIsRefused,
                        ABackToBackLessonIsAccepted);

template<class World>
class AvailabilityRepositoryContract : public ::testing::Test {
protected:
    World world_;
};

PDR_CONTRACT_SUITE_P(AvailabilityRepositoryContract);

/// Не задавал доступность и задал пустую — разные ответы. Первое значит «занятия
/// просто требуют подтверждения», второе — «он никогда не работает».
PDR_CONTRACT_TEST_P(AvailabilityRepositoryContract, WhatWasNeverSetIsAbsent) {
    auto& availability = this->world_.Availability();

    EXPECT_FALSE(availability.Of(ContractGround::Tenant(), ContractGround::Tutor()).has_value());
}

PDR_CONTRACT_TEST_P(AvailabilityRepositoryContract, RulesComeBackAsTheyWereWritten) {
    auto& storage = this->world_.Availability();
    const auto monday = AvailabilityRule::Compose(core::Weekday::kMonday,
                                                  core::LocalTime::Compose(10, 0).Value(),
                                                  core::LocalTime::Compose(18, 30).Value(),
                                                  ContractGround::Zone())
                            .Value();
    const auto written = Availability::Compose({monday}, {}).Value();

    ASSERT_TRUE(
        storage.Replace(ContractGround::Tenant(), ContractGround::Tutor(), written).HasValue());

    const auto read = storage.Of(ContractGround::Tenant(), ContractGround::Tutor());
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->Rules().size(), 1U);
    EXPECT_EQ(read->Rules().front().Day(), core::Weekday::kMonday);
    EXPECT_EQ(read->Rules().front().From(), core::LocalTime::Compose(10, 0).Value());
    EXPECT_EQ(read->Rules().front().To(), core::LocalTime::Compose(18, 30).Value());
    EXPECT_EQ(read->Rules().front().Zone().Name(), ContractGround::Zone().Name());
}

PDR_CONTRACT_TEST_P(AvailabilityRepositoryContract, ADayOffKeepsItsEmptyHours) {
    auto& storage = this->world_.Availability();
    const auto off = AvailabilityException{core::Date::Compose(2026, 3, 2).Value(), std::nullopt};
    const auto other =
        AvailabilityException{core::Date::Compose(2026, 3, 3).Value(),
                              ContractGround::Window(ContractGround::Utc(2026, 3, 3, 14),
                                                     ContractGround::Utc(2026, 3, 3, 16))};

    ASSERT_TRUE(storage
                    .Replace(ContractGround::Tenant(),
                             ContractGround::Tutor(),
                             Availability::Compose({}, {off, other}).Value())
                    .HasValue());

    const auto read = storage.Of(ContractGround::Tenant(), ContractGround::Tutor());
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->Exceptions().size(), 2U);
    EXPECT_FALSE(read->Exceptions().front().instead.has_value());
    ASSERT_TRUE(read->Exceptions().back().instead.has_value());
    EXPECT_TRUE(read->Exceptions().back().instead->From() == ContractGround::Utc(2026, 3, 3, 14));
}

/// Запись целиком означает, что прежнее исчезает. Иначе «убрал вторник» не
/// выражается вовсе.
PDR_CONTRACT_TEST_P(AvailabilityRepositoryContract, WritingAgainReplacesEverything) {
    auto& storage = this->world_.Availability();
    const auto monday = AvailabilityRule::Compose(core::Weekday::kMonday,
                                                  core::LocalTime::Compose(10, 0).Value(),
                                                  core::LocalTime::Compose(18, 0).Value(),
                                                  ContractGround::Zone())
                            .Value();
    const auto friday = AvailabilityRule::Compose(core::Weekday::kFriday,
                                                  core::LocalTime::Compose(9, 0).Value(),
                                                  core::LocalTime::Compose(12, 0).Value(),
                                                  ContractGround::Zone())
                            .Value();

    ASSERT_TRUE(storage
                    .Replace(ContractGround::Tenant(),
                             ContractGround::Tutor(),
                             Availability::Compose({monday, friday}, {}).Value())
                    .HasValue());
    ASSERT_TRUE(storage
                    .Replace(ContractGround::Tenant(),
                             ContractGround::Tutor(),
                             Availability::Compose({friday}, {}).Value())
                    .HasValue());

    const auto read = storage.Of(ContractGround::Tenant(), ContractGround::Tutor());
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->Rules().size(), 1U);
    EXPECT_EQ(read->Rules().front().Day(), core::Weekday::kFriday);
}

PDR_CONTRACT_REGISTER_P(AvailabilityRepositoryContract,
                        WhatWasNeverSetIsAbsent,
                        RulesComeBackAsTheyWereWritten,
                        ADayOffKeepsItsEmptyHours,
                        WritingAgainReplacesEverything);

template<class World>
class RecurrenceRepositoryContract : public ::testing::Test {
protected:
    World world_;

    RecurrenceSeries ASeries() const {
        return RecurrenceSeries::Compose(
                   this->world_.SeriesId(),
                   ContractGround::Tenant(),
                   ContractGround::Tutor(),
                   {ContractGround::Student()},
                   RecurrenceRule::Parse("FREQ=WEEKLY;BYDAY=TU;COUNT=8").Value(),
                   core::Date::Compose(2026, 3, 3).Value(),
                   core::LocalTime::Compose(18, 0).Value(),
                   ContractGround::Zone(),
                   std::chrono::minutes{60})
            .Value();
    }
};

PDR_CONTRACT_SUITE_P(RecurrenceRepositoryContract);

PDR_CONTRACT_TEST_P(RecurrenceRepositoryContract, WhatWasNeverCreatedIsAbsent) {
    EXPECT_FALSE(
        this->world_.Series().Find(ContractGround::Tenant(), this->world_.SeriesId()).has_value());
}

/// СЕРИЯ ВОЗВРАЩАЕТСЯ ПРАВИЛОМ, А НЕ СПИСКОМ. Проверяется именно правило: если
/// хранилище когда-нибудь развернёт серию в строки, эта проверка останется
/// зелёной ровно до первого переноса — а до тех пор соврёт.
PDR_CONTRACT_TEST_P(RecurrenceRepositoryContract, TheSeriesComesBackAsTheRuleItWas) {
    auto& series = this->world_.Series();
    const auto written = this->ASeries();

    ASSERT_TRUE(series.Create(written).HasValue());

    const auto read = series.Find(ContractGround::Tenant(), written.Id());
    ASSERT_TRUE(read.has_value());
    EXPECT_EQ(read->Rule().ToRRule(), "FREQ=WEEKLY;BYDAY=TU;COUNT=8");
    EXPECT_EQ(read->StartsOn(), core::Date::Compose(2026, 3, 3).Value());
    EXPECT_EQ(read->At(), core::LocalTime::Compose(18, 0).Value());
    EXPECT_EQ(read->Zone().Name(), ContractGround::Zone().Name());
    EXPECT_EQ(read->LessonDuration(), std::chrono::minutes{60});
    ASSERT_EQ(read->Participants().size(), 1U);
    EXPECT_TRUE(read->Participants().front() == ContractGround::Student());
}

PDR_CONTRACT_TEST_P(RecurrenceRepositoryContract, ACancelledOccurrenceComesBackWithTheSeries) {
    auto& series = this->world_.Series();
    const auto written = this->ASeries();
    ASSERT_TRUE(series.Create(written).HasValue());

    const auto on = core::Date::Compose(2026, 3, 17).Value();
    ASSERT_TRUE(series
                    .Record(ContractGround::Tenant(),
                            written.Id(),
                            RecurrenceException{on, ExceptionKind::kCancelled})
                    .HasValue());

    const auto read = series.Find(ContractGround::Tenant(), written.Id());
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->Exceptions().size(), 1U);
    EXPECT_EQ(read->Exceptions().front().occurrence_on, on);
    EXPECT_EQ(read->Exceptions().front().kind, ExceptionKind::kCancelled);
    EXPECT_FALSE(read->Exceptions().front().moved_to.has_value());
}

PDR_CONTRACT_TEST_P(RecurrenceRepositoryContract, AMovedOccurrenceKeepsItsNewPlace) {
    auto& series = this->world_.Series();
    const auto written = this->ASeries();
    ASSERT_TRUE(series.Create(written).HasValue());

    const auto on = core::Date::Compose(2026, 3, 17).Value();
    const auto moved_to = ContractGround::Utc(2026, 3, 18, 9);
    ASSERT_TRUE(series
                    .Record(ContractGround::Tenant(),
                            written.Id(),
                            RecurrenceException{
                                on, ExceptionKind::kMoved, moved_to, std::chrono::minutes{90}})
                    .HasValue());

    const auto read = series.Find(ContractGround::Tenant(), written.Id());
    ASSERT_TRUE(read.has_value());
    ASSERT_EQ(read->Exceptions().size(), 1U);
    EXPECT_EQ(read->Exceptions().front().kind, ExceptionKind::kMoved);
    ASSERT_TRUE(read->Exceptions().front().moved_to.has_value());
    EXPECT_TRUE(*read->Exceptions().front().moved_to == moved_to);
    ASSERT_TRUE(read->Exceptions().front().moved_duration.has_value());
    EXPECT_EQ(*read->Exceptions().front().moved_duration, std::chrono::minutes{90});
}

PDR_CONTRACT_TEST_P(RecurrenceRepositoryContract, ASecondExceptionOnOneOccurrenceIsRefused) {
    auto& series = this->world_.Series();
    const auto written = this->ASeries();
    ASSERT_TRUE(series.Create(written).HasValue());

    const auto on = core::Date::Compose(2026, 3, 17).Value();
    ASSERT_TRUE(series
                    .Record(ContractGround::Tenant(),
                            written.Id(),
                            RecurrenceException{on, ExceptionKind::kCancelled})
                    .HasValue());

    const auto refused = series.Record(
        ContractGround::Tenant(), written.Id(), RecurrenceException{on, ExceptionKind::kCancelled});

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "recurrence_exception_repeated");
}

PDR_CONTRACT_REGISTER_P(RecurrenceRepositoryContract,
                        WhatWasNeverCreatedIsAbsent,
                        TheSeriesComesBackAsTheRuleItWas,
                        ACancelledOccurrenceComesBackWithTheSeries,
                        AMovedOccurrenceKeepsItsNewPlace,
                        ASecondExceptionOnOneOccurrenceIsRefused);

}  // namespace pdr::scheduling::testing

/// Инстанцировать все три набора для одного мира.
#define PDR_SCHEDULING_CONTRACT(prefix, world)                                                   \
    PDR_CONTRACT_INSTANTIATE_P(prefix, LessonRepositoryContract, ::testing::Types<world>);       \
    PDR_CONTRACT_INSTANTIATE_P(prefix, AvailabilityRepositoryContract, ::testing::Types<world>); \
    PDR_CONTRACT_INSTANTIATE_P(prefix, RecurrenceRepositoryContract, ::testing::Types<world>)
