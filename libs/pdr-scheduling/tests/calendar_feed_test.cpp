#include "scheduling/core/calendar_feed.hpp"

#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "builders/identifiers.hpp"
#include "builders/lesson_builder.hpp"
#include "builders/moment_builder.hpp"
#include "fakes/fake_clock.hpp"
#include "fakes/fake_id_generator.hpp"
#include "fakes/fake_scheduling.hpp"
#include "fakes/fake_secret_generator.hpp"
#include "scheduling/application/issue_calendar_feed.hpp"
#include "scheduling/application/read_calendar_feed.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::scheduling::testing::FakeCalendarFeeds;
using pdr::scheduling::testing::FakeLessons;
using pdr::scheduling::testing::FakeSeries;
using pdr::scheduling::testing::FakeTimeZoneRules;
using pdr::scheduling::testing::LessonBuilder;
using pdr::testing::FakeClock;
using pdr::testing::FakeSecretGenerator;
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

core::TimeZone Moscow() {
    return core::TimeZone::Parse("Europe/Moscow").value();
}

core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, 0).Build();
}

/// Счёт отпечатка без userver: SHA-256 живёт в нём, а unit-прогон собирается
/// без него. Подмена честная — фейк отличает разные строки друг от друга, а
/// больше от отпечатка в этих проверках ничего не требуется.
class FakeDigests final : public application::ports::Digests {
public:
    core::Digest Of(std::string_view text) const override {
        std::string hex;
        std::size_t rolling = 0;
        for (const char symbol : text) {
            rolling = rolling * 131 + static_cast<unsigned char>(symbol);
        }
        for (int index = 0; index < 8; ++index) {
            for (int nibble = 0; nibble < 8; ++nibble) {
                hex += "0123456789abcdef"[(rolling >> ((nibble * 4) % 60)) & 0xFU];
            }
            rolling = rolling * 1000003 + static_cast<std::size_t>(index);
        }
        return core::Digest::Parse(hex).Value();
    }
};

TEST(CalendarFeedTest, AShortLinkIsRefusedRatherThanAccepted) {
    const auto short_one = CalendarFeedSecret::Parse("abc");

    ASSERT_FALSE(short_one.HasValue());
    EXPECT_EQ(short_one.Failure().Code(), "calendar_secret_too_short");
}

TEST(CalendarFeedTest, ALinkWithForeignSymbolsIsRefused) {
    const auto broken = CalendarFeedSecret::Parse(std::string(43, '/'));

    ASSERT_FALSE(broken.HasValue());
    EXPECT_EQ(broken.Failure().Code(), "calendar_secret_malformed");
}

TEST(CalendarFeedTest, TheHorizonLooksBackAndForward) {
    const auto now = Utc(2026, 8, 1, 12);

    const auto horizon = FeedHorizon(now);

    EXPECT_LT(horizon.From(), now);
    EXPECT_GT(horizon.To(), now);
    EXPECT_TRUE(horizon.Contains(now));
}

class IssueCalendarFeedTest : public ::testing::Test {
protected:
    IssueCalendarFeed Issuing() {
        return IssueCalendarFeed{feeds_, secrets_, digests_, clock_};
    }

    FakeClock clock_{Utc(2026, 7, 20, 12)};
    FakeCalendarFeeds feeds_;
    FakeSecretGenerator secrets_;
    FakeDigests digests_;
};

/// СЕКРЕТ ОТДАЁТСЯ ОДИН РАЗ, а в хранилище уезжает отпечаток. Второй раз
/// показать ссылку неоткуда — это не забывчивость, а то самое свойство, ради
/// которого отпечаток и хранится.
TEST_F(IssueCalendarFeedTest, TheSecretIsHandedOverOnceAndStoredAsItsFingerprint) {
    const auto issued = Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(issued.HasValue());
    EXPECT_GE(issued.Value().secret.Value().size(), CalendarFeedSecret::kLeastLength);
    EXPECT_EQ(issued.Value().feed.Secret(), digests_.Of(issued.Value().secret.Value()));

    const auto kept = feeds_.ByDigest(Tenant(), issued.Value().feed.Secret());
    ASSERT_TRUE(kept.has_value());
    EXPECT_TRUE(kept->Person() == Tutor());
}

TEST_F(IssueCalendarFeedTest, NamesAreNotShownUntilTheHumanAsks) {
    const auto issued = Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(issued.HasValue());
    EXPECT_EQ(issued.Value().feed.Naming(), CalendarNaming::kWithoutNames);
}

/// ГЛАВНАЯ ПРОВЕРКА ЗАДАЧИ: перевыпуск делает старую ссылку нерабочей, и делает
/// это ОДНИМ действием — тем же, которым ссылка выдавалась впервые.
TEST_F(IssueCalendarFeedTest, ReissuingKillsTheOldLinkInOneStep) {
    const auto first = Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()});
    ASSERT_TRUE(first.HasValue());

    const auto second = Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(second.HasValue());
    EXPECT_NE(first.Value().secret.Value(), second.Value().secret.Value())
        << "перевыпуск выдал ту же ссылку";
    EXPECT_FALSE(feeds_.ByDigest(Tenant(), first.Value().feed.Secret()).has_value())
        << "старая ссылка всё ещё открывает расписание";
    EXPECT_TRUE(feeds_.ByDigest(Tenant(), second.Value().feed.Secret()).has_value());
    EXPECT_EQ(feeds_.Size(), 1U) << "перевыпуск завёл вторую подписку вместо замены";
}

TEST_F(IssueCalendarFeedTest, ReissuingKeepsTheSettingTheHumanChose) {
    ASSERT_TRUE(Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()}).HasValue());
    ASSERT_TRUE(feeds_.Rename(Tenant(), Tutor(), CalendarNaming::kWithNames).HasValue());

    const auto again = Issuing().Execute(IssueCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(again.HasValue());
    EXPECT_EQ(feeds_.NamingOf(Tenant(), Tutor()), CalendarNaming::kWithNames)
        << "починка утечки заодно поменяла настройку";
}

class ReadCalendarFeedTest : public ::testing::Test {
protected:
    ReadCalendarFeedTest() {
        zones_.Say(Moscow(), core::ZoneOffsets::Fixed(3h));
        EXPECT_TRUE(
            feeds_
                .Issue(CalendarFeed::Compose(
                           Tenant(), Tutor(), digests_.Of("secret"), CalendarNaming::kWithoutNames)
                           .Value(),
                       clock_.Now())
                .HasValue());
    }

    void Book(int number, core::Instant starts_at) {
        EXPECT_TRUE(lessons_
                        .Save(LessonBuilder{}
                                  .Id(Numbered<core::LessonId>(number))
                                  .InTenant(Tenant())
                                  .Between(Tutor(), Student())
                                  .StartingAt(starts_at)
                                  .InZone(Moscow())
                                  .Build())
                        .HasValue());
    }

    ReadCalendarFeed Reading() {
        return ReadCalendarFeed{
            feeds_,
            lessons_,
            series_,
            zones_,
            digests_,
            clock_,
            CalendarWording{"Поля", "Занятие", "Занятие: {name}"},
        };
    }

    FakeClock clock_{Utc(2026, 8, 1, 12)};
    FakeCalendarFeeds feeds_;
    FakeLessons lessons_;
    FakeSeries series_;
    FakeTimeZoneRules zones_;
    FakeDigests digests_;
};

TEST_F(ReadCalendarFeedTest, TheLessonsOfThePersonAreInTheFeed) {
    Book(1, Utc(2026, 8, 4, 15));

    const auto read = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(read.HasValue());
    EXPECT_NE(read.Value().calendar.find("DTSTART;TZID=Europe/Moscow:20260804T180000"),
              std::string::npos)
        << read.Value().calendar;
    EXPECT_NE(read.Value().calendar.find("UID:lesson-"), std::string::npos);
}

/// ОТМЕНЁННОЕ ЗАНЯТИЕ В ЛЕНТЕ НЕ ПОКАЗЫВАЕТСЯ: человек увидел бы в своём
/// календаре занятие, которого не будет, и пришёл бы на него.
TEST_F(ReadCalendarFeedTest, WhatWasCancelledIsNotShown) {
    Book(1, Utc(2026, 8, 4, 15));
    const auto lesson = lessons_.Find(Tenant(), Numbered<core::LessonId>(1));
    ASSERT_TRUE(lesson.has_value());
    const auto cancelled =
        lesson->CancelByTutor(core::CurrencyCode::Parse("RUB").value(), Tutor(), clock_.Now());
    ASSERT_TRUE(cancelled.HasValue());
    ASSERT_TRUE(lessons_.SetState(cancelled.Value().lesson).HasValue());

    const auto read = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(read.HasValue());
    EXPECT_EQ(read.Value().calendar.find("BEGIN:VEVENT"), std::string::npos)
        << "отменённое занятие уехало в календарь";
}

/// СЕРИЯ УЕЗЖАЕТ ПРАВИЛОМ — ТЕМ ЖЕ, КОТОРЫМ ХРАНИТСЯ. Второго генератора
/// повторов в дереве нет, и здесь видно, что он не завёлся.
TEST_F(ReadCalendarFeedTest, ASeriesTravelsAsItsOwnRule) {
    const auto series =
        RecurrenceSeries::Compose(
            Numbered<core::SeriesId>(1),
            Tenant(),
            Tutor(),
            {Student()},
            RecurrenceRule::Compose(1, {core::Weekday::kTuesday}, Ending{Count{8}}).Value(),
            core::Date::Compose(2026, 8, 1).Value(),
            core::LocalTime::Compose(18, 0).Value(),
            Moscow(),
            60min)
            .Value();
    ASSERT_TRUE(series_.Create(series).HasValue());

    const auto read = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(read.HasValue());
    EXPECT_NE(read.Value().calendar.find("RRULE:" + series.Rule().ToRRule()), std::string::npos)
        << read.Value().calendar;

    /// Первое вхождение — вторник четвёртого, а не суббота первого: серия
    /// начинается тогда, когда её ставит правило.
    EXPECT_NE(read.Value().calendar.find("DTSTART;TZID=Europe/Moscow:20260804T180000"),
              std::string::npos);
}

TEST_F(ReadCalendarFeedTest, ACancelledOccurrenceLeavesTheSeriesByExdate) {
    const auto series =
        RecurrenceSeries::Compose(
            Numbered<core::SeriesId>(1),
            Tenant(),
            Tutor(),
            {Student()},
            RecurrenceRule::Compose(1, {core::Weekday::kTuesday}, Ending{Count{8}}).Value(),
            core::Date::Compose(2026, 8, 1).Value(),
            core::LocalTime::Compose(18, 0).Value(),
            Moscow(),
            60min)
            .Value();
    ASSERT_TRUE(series_.Create(series).HasValue());
    ASSERT_TRUE(series_
                    .Record(Tenant(),
                            series.Id(),
                            RecurrenceException{core::Date::Compose(2026, 8, 11).Value(),
                                                ExceptionKind::kCancelled})
                    .HasValue());

    const auto read = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(read.HasValue());
    EXPECT_NE(read.Value().calendar.find("EXDATE;TZID=Europe/Moscow:20260811T180000"),
              std::string::npos)
        << read.Value().calendar;
}

/// ОТПЕЧАТОК СОДЕРЖИМОГО НЕ МЕНЯЕТСЯ САМ ПО СЕБЕ. Календарь ходит сюда раз в
/// час и чаще; отпечаток, меняющийся на каждом обращении, не даёт ему ни разу
/// обойтись условным запросом — то есть кэширования нет вовсе.
TEST_F(ReadCalendarFeedTest, TheFingerprintHoldsStillWhileTheScheduleDoes) {
    Book(1, Utc(2026, 8, 4, 15));

    const auto first = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});
    clock_.Advance(90s);
    const auto second = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});

    ASSERT_TRUE(first.HasValue());
    ASSERT_TRUE(second.HasValue());
    EXPECT_EQ(first.Value().etag, second.Value().etag);

    Book(2, Utc(2026, 8, 5, 15));
    const auto third = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Tutor()});
    ASSERT_TRUE(third.HasValue());
    EXPECT_NE(first.Value().etag, third.Value().etag)
        << "лента поменялась, а отпечаток остался прежним";
}

TEST_F(ReadCalendarFeedTest, WithoutASubscriptionThereIsNoFeed) {
    const auto missing = Reading().Execute(ReadCalendarFeed::Request{Tenant(), Student()});

    ASSERT_FALSE(missing.HasValue());
    EXPECT_EQ(missing.Failure().Code(), "calendar_feed_not_found");
}

}  // namespace
}  // namespace pdr::scheduling
