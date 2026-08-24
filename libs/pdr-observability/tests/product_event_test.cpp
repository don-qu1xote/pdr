#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "fake_product_event_stream.hpp"
#include "fakes/fake_clock.hpp"
#include "observability/application/contract_service.hpp"

/// @file
/// Сценарии реестра продуктовых событий: по одному на каждый тип из
/// configs/product-events.yaml. Тип, не встречающийся здесь, роняет сборку —
/// scripts/check_product_events.py требует сценарий у каждого события, потому
/// что «событие есть в реестре» и «событие пишется» — разные утверждения.

namespace pdr::observability {
namespace {

using pdr::observability::testing::FakeProductEventStream;
using pdr::testing::Numbered;

class ProductEventTest : public ::testing::Test {
protected:
    core::Result<void> Record(std::string_view type, int version, Role actor, Fields fields) {
        return service_.Record(tenant_, type, version, actor, clock_.Now(), std::move(fields));
    }

    pdr::testing::FakeClock clock_;
    FakeProductEventStream stream_;
    ContractService service_{stream_};

    core::TenantId tenant_{Numbered<core::TenantId>(1)};
};

TEST_F(ProductEventTest, EveryScenarioOfTheRegistryIsWritten) {
    EXPECT_TRUE(Record("notes.note_published",
                       1,
                       Role::kTutor,
                       {{"minutes_after_lesson", Value::Minutes(35)},
                        {"draft_edited_share", Value::Bucket("half")},
                        {"had_transcript", Value::Flag(true)}}));

    EXPECT_TRUE(Record("scheduling.lesson_cancelled",
                       1,
                       Role::kGuardian,
                       {{"hours_before_start", Value::Hours(30)},
                        {"initiator_role", Value::Code("guardian")},
                        {"within_free_window", Value::Flag(true)}}));

    EXPECT_TRUE(Record("scheduling.lesson_rescheduled",
                       1,
                       Role::kTutor,
                       {{"hours_before_start", Value::Hours(50)},
                        {"initiator_role", Value::Code("tutor")},
                        {"shift_hours", Value::Hours(24)}}));

    EXPECT_TRUE(Record("scheduling.lesson_completed",
                       1,
                       Role::kTutor,
                       {{"ordinal_for_student", Value::Count(2)},
                        {"lessons_same_day", Value::Count(1)},
                        {"booked_hours_ahead", Value::Hours(72)}}));

    EXPECT_TRUE(Record("billing.autocharge_objected",
                       1,
                       Role::kGuardian,
                       {{"hours_after_notice", Value::Hours(12)},
                        {"within_window", Value::Flag(true)},
                        {"payer_role", Value::Code("guardian")}}));

    EXPECT_TRUE(Record("content.material_opened",
                       1,
                       Role::kStudent,
                       {{"material", Value::Reference(Numbered<core::MaterialId>(7))},
                        {"open_ordinal", Value::Count(3)},
                        {"days_since_first_open", Value::Days(9)},
                        {"reader_role", Value::Code("student")}}));

    EXPECT_TRUE(Record("content.plan_state",
                       1,
                       Role::kTutor,
                       {{"has_plan", Value::Flag(false)},
                        {"lessons_done", Value::Bucket("few")},
                        {"plan_followed", Value::Flag(false)}}));

    EXPECT_TRUE(Record(
        "identity.self_account_granted",
        1,
        Role::kGuardian,
        {{"student_age_years", Value::Count(14)}, {"lessons_before", Value::Bucket("many")}}));

    EXPECT_TRUE(Record("identity.self_account_revoked",
                       1,
                       Role::kGuardian,
                       {{"student_age_years", Value::Count(14)},
                        {"days_granted", Value::Days(21)},
                        {"reason", Value::Code("missed_lessons")}}));

    EXPECT_TRUE(Record("reputation.rating_recorded",
                       1,
                       Role::kStudent,
                       {{"score", Value::Score(5)},
                        {"low_share_in_window", Value::Bucket("low")},
                        {"lessons_before_rating", Value::Count(4)},
                        {"with_text", Value::Flag(false)}}));

    EXPECT_TRUE(Record("matching.newcomer_shown",
                       1,
                       Role::kStudent,
                       {{"position_bucket", Value::Bucket("middle")},
                        {"results_shown", Value::Count(20)},
                        {"newcomers_shown", Value::Count(4)}}));

    EXPECT_TRUE(Record("practice.mistake_repeated",
                       1,
                       Role::kStudent,
                       {{"attempts", Value::Count(3)},
                        {"window_attempts", Value::Count(10)},
                        {"skill", Value::Reference(Numbered<core::SkillId>(2))}}));

    EXPECT_EQ(stream_.Recorded().size(), 12U)
        << "сценарий реестра, который не пишется, отвечает на вопрос молчанием";
}

/// Первое из двух событий, без которых вопрос не закроется никогда: юридическая
/// половина порога 14/16 закрыта разбором права, поведенческая — только парой
/// «включил» и «отключил». Опрос её не заменяет: опрошенный отвечает, как он
/// ХОТЕЛ БЫ поступить.
TEST_F(ProductEventTest, RollbackOfSelfAccountIsMeasurableOnlyAsAPair) {
    ASSERT_TRUE(Record(
        "identity.self_account_granted",
        1,
        Role::kGuardian,
        {{"student_age_years", Value::Count(14)}, {"lessons_before", Value::Bucket("few")}}));
    ASSERT_TRUE(Record("identity.self_account_revoked",
                       1,
                       Role::kGuardian,
                       {{"student_age_years", Value::Count(14)},
                        {"days_granted", Value::Days(18)},
                        {"reason", Value::Code("no_reason")}}));

    const auto granted = stream_.Last("identity.self_account_granted");
    const auto revoked = stream_.Last("identity.self_account_revoked");
    ASSERT_TRUE(granted.has_value());
    ASSERT_TRUE(revoked.has_value());

    ASSERT_TRUE(revoked->Field("days_granted").has_value());
    EXPECT_EQ(revoked->Field("days_granted")->Number(), 18);
    EXPECT_EQ(granted->Field("days_granted"), std::nullopt)
        << "у включения срока нет: он появляется только когда права отобрали";

    for (const auto& event : stream_.Recorded()) {
        EXPECT_EQ(AnonymityBreach(event.AllFields()), std::nullopt);
    }
}

/// Второе: инфляцию оценок надо увидеть ДО того, как все оценки станут
/// пятёрками. После этого различать некого, а исторических данных не появится.
/// Долю низких оценок за скользящее окно считает издатель — он знает окно,
/// событию хватает корзины.
TEST_F(ProductEventTest, RatingCarriesTheShareOfLowScoresInTheWindow) {
    const std::vector<std::pair<std::int64_t, std::string>> ratings{
        {4, "noticeable"},
        {5, "low"},
        {5, "low"},
        {5, "none"},
    };

    for (const auto& [score, share] : ratings) {
        ASSERT_TRUE(Record("reputation.rating_recorded",
                           1,
                           Role::kStudent,
                           {{"score", Value::Score(score)},
                            {"low_share_in_window", Value::Bucket(share)},
                            {"lessons_before_rating", Value::Count(1)},
                            {"with_text", Value::Flag(false)}}));
    }

    ASSERT_EQ(stream_.Recorded().size(), ratings.size());
    for (std::size_t index = 0; index < ratings.size(); ++index) {
        const auto& recorded = stream_.Recorded()[index];
        ASSERT_TRUE(recorded.Field("low_share_in_window").has_value());
        EXPECT_EQ(recorded.Field("low_share_in_window")->Text(), ratings[index].second)
            << "доля считается в момент записи: пересчитать её задним числом будет не из чего";
    }
}

TEST_F(ProductEventTest, NoFieldNamesAPerson) {
    for (const auto& event : stream_.Recorded()) {
        EXPECT_EQ(AnonymityBreach(event.AllFields()), std::nullopt);
    }

    EXPECT_FALSE(Record(
        "scheduling.lesson_cancelled", 1, Role::kTutor, {{"student_id", Value::Code("s-1")}}));
    EXPECT_FALSE(Record("scheduling.lesson_cancelled",
                        1,
                        Role::kTutor,
                        {{"tutor_email", Value::Code("kto@example.test")}}));
    EXPECT_FALSE(Record(
        "scheduling.lesson_cancelled", 1, Role::kTutor, {{"display_name", Value::Code("Пётр")}}));
    EXPECT_FALSE(Record("scheduling.lesson_cancelled", 1, Role::kTutor, {{"id", Value::Count(1)}}));

    EXPECT_TRUE(stream_.Recorded().empty()) << "отказ не должен доходить до потока";
}

/// Идентификатор, спрятанный в код, — тот же идентификатор. Ссылке им быть
/// можно: человеком она не бывает по устройству типа, и это проверяет
/// компилятор (цель pdr_compile_fail_person_in_product_event).
TEST_F(ProductEventTest, CodeIsNotAHidingPlaceForAnIdentifier) {
    const auto person = Numbered<core::PersonId>(42);

    EXPECT_FALSE(Record("content.material_opened",
                        1,
                        Role::kStudent,
                        {{"reader_role", Value::Code(person.ToString())}}));
    EXPECT_FALSE(Record("content.material_opened",
                        1,
                        Role::kStudent,
                        {{"lessons_done", Value::Bucket(person.ToString())}}));

    EXPECT_TRUE(Record("content.material_opened",
                       1,
                       Role::kStudent,
                       {{"material", Value::Reference(Numbered<core::MaterialId>(3))},
                        {"open_ordinal", Value::Count(1)},
                        {"days_since_first_open", Value::Days(0)},
                        {"reader_role", Value::Code("student")}}));
}

/// Смена версии схемы не ломает чтение старых записей — ни в одну сторону.
/// Так выглядит день после переименования поля: в таблице лежат записи обеих
/// версий, и читают их обе одним и тем же кодом.
TEST_F(ProductEventTest, VersionChangeKeepsBothGenerationsReadable) {
    ASSERT_TRUE(Record("scheduling.lesson_cancelled",
                       1,
                       Role::kGuardian,
                       {{"hours_before_start", Value::Hours(30)},
                        {"initiator_role", Value::Code("guardian")},
                        {"within_free_window", Value::Flag(true)}}));

    ASSERT_TRUE(Record("scheduling.lesson_cancelled",
                       2,
                       Role::kGuardian,
                       {{"hours_before_start", Value::Hours(30)},
                        {"initiator_role", Value::Code("guardian")},
                        {"free_window_code", Value::Code("free")}}));

    ASSERT_EQ(stream_.Recorded().size(), 2U);
    const auto& old_record = stream_.Recorded().front();
    const auto& new_record = stream_.Recorded().back();

    EXPECT_EQ(old_record.Version(), 1);
    EXPECT_EQ(new_record.Version(), 2);

    for (const auto& record : stream_.Recorded()) {
        ASSERT_TRUE(record.Field("hours_before_start").has_value())
            << "поле, пережившее смену версии, читается у обеих";
        EXPECT_EQ(record.Field("hours_before_start")->Number(), 30);
    }

    EXPECT_TRUE(old_record.Field("within_free_window").has_value());
    EXPECT_EQ(old_record.Field("free_window_code"), std::nullopt)
        << "читатель новой схемы не падает на старой записи — поля просто нет";

    EXPECT_TRUE(new_record.Field("free_window_code").has_value());
    EXPECT_EQ(new_record.Field("within_free_window"), std::nullopt)
        << "и наоборот: читатель старой схемы не падает на новой записи";
}

TEST_F(ProductEventTest, RefusesWhatCannotAnswerAnything) {
    EXPECT_FALSE(Record("lesson_cancelled", 1, Role::kTutor, {{"attempts", Value::Count(1)}}))
        << "имя без контекста-издателя";
    EXPECT_FALSE(
        Record("Scheduling.LessonCancelled", 1, Role::kTutor, {{"attempts", Value::Count(1)}}));
    EXPECT_FALSE(
        Record("scheduling.lesson_cancelled", 0, Role::kTutor, {{"attempts", Value::Count(1)}}))
        << "схема без версии запрещена: менять её придётся, и не один раз";
    EXPECT_FALSE(Record("scheduling.lesson_cancelled", 1, Role::kTutor, {}))
        << "событие без полей ничего не измеряет";

    const auto refusal = Record(
        "scheduling.lesson_cancelled", 1, Role::kTutor, {{"person_role", Value::Code("tutor")}});
    ASSERT_FALSE(refusal);
    EXPECT_EQ(refusal.Failure().Code(), "person_in_product_event");
    EXPECT_EQ(refusal.Failure().Kind(), core::ErrorKind::kValidation);
}

TEST_F(ProductEventTest, TenantAndRoleAreAllTheEventKnowsAboutTheHuman) {
    ASSERT_TRUE(Record("notes.note_published",
                       1,
                       Role::kTutor,
                       {{"minutes_after_lesson", Value::Minutes(10)},
                        {"draft_edited_share", Value::Bucket("none")},
                        {"had_transcript", Value::Flag(true)}}));

    ASSERT_EQ(stream_.Recorded().size(), 1U);
    const auto& recorded = stream_.Recorded().front();

    EXPECT_TRUE(recorded.Tenant() == tenant_);
    EXPECT_EQ(Name(recorded.Actor()), "tutor");
    EXPECT_EQ(recorded.OccurredAt(), clock_.Now());
}

TEST_F(ProductEventTest, AskingForTheWrongShapeOfValueIsAProgrammerError) {
    EXPECT_THROW((void)Value::Flag(true).Number(), std::logic_error);
    EXPECT_THROW((void)Value::Count(1).Yes(), std::logic_error);
    EXPECT_EQ(Name(ValueKind::kReference), "reference");
}

}  // namespace
}  // namespace pdr::observability
