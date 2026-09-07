#include "scheduling/core/icalendar.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "builders/moment_builder.hpp"

namespace pdr::scheduling {
namespace {

using namespace std::chrono_literals;
using pdr::testing::MomentBuilder;

core::TimeZone Berlin() {
    return core::TimeZone::Parse("Europe/Berlin").value();
}

core::TimeZone Moscow() {
    return core::TimeZone::Parse("Europe/Moscow").value();
}

core::Instant Utc(int year, unsigned month, unsigned day, unsigned hour) {
    return MomentBuilder{}.Utc(year, month, day).At(hour, 0).Build();
}

/// Берлин 2026-го: с +01:00 на +02:00 двадцать девятого марта и обратно
/// двадцать пятого октября. Те самые значения, которые отдаёт база.
core::ZoneOffsets BerlinRules() {
    return core::ZoneOffsets::Compose(1h,
                                      {core::OffsetShift{Utc(2026, 3, 29, 1), 2h},
                                       core::OffsetShift{Utc(2026, 10, 25, 1), 1h}})
        .Value();
}

std::vector<std::string> LinesOf(const std::string& text) {
    std::vector<std::string> lines;
    std::string current;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n') {
            lines.push_back(current);
            current.clear();
            ++index;
            continue;
        }
        current += text[index];
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

bool Has(const std::string& text, std::string_view line) {
    const auto lines = LinesOf(text);
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

CalendarFeedContents WithOneLesson() {
    CalendarFeedContents contents{"Поля", Utc(2026, 7, 1, 12), {}, {}};
    contents.zones.emplace_back(Berlin(), BerlinRules());
    contents.entries.push_back(CalendarEntry{"lesson-1@pdr",
                                             Utc(2026, 8, 4, 16),
                                             60min,
                                             Berlin(),
                                             "Занятие",
                                             "https://example.test/room",
                                             std::string{},
                                             {}});
    return contents;
}

TEST(ICalendarTest, TheFeedOpensAndClosesAsTheStandardWants) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    const auto lines = LinesOf(built.Value());
    ASSERT_FALSE(lines.empty());
    EXPECT_EQ(lines.front(), "BEGIN:VCALENDAR");
    EXPECT_EQ(lines.back(), "END:VCALENDAR");
    EXPECT_TRUE(Has(built.Value(), "VERSION:2.0"));
    EXPECT_TRUE(Has(built.Value(), "CALSCALE:GREGORIAN"));
}

/// ПЕРЕВОДЫ СТРОК — ТОЛЬКО CRLF. Календарь, которому прислали голый перевод
/// строки, читает ленту наполовину и молчит об этом.
TEST(ICalendarTest, EveryLineEndsWithCarriageReturnAndLineFeed) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    for (std::size_t index = 0; index + 1 < built.Value().size(); ++index) {
        if (built.Value()[index] == '\n') {
            EXPECT_EQ(built.Value()[index - 1], '\r') << "перевод строки без возврата каретки";
        }
    }
}

/// ГЛАВНАЯ ПРОВЕРКА ЧАСОВ: время события записано МЕСТНОЕ, а зона названа
/// параметром. Занятие в 18:00 по Берлину остаётся в 18:00 и после перевода
/// часов — потому что в ленте написано «18:00 в Europe/Berlin», а не момент.
TEST(ICalendarTest, TheEventCarriesLocalTimeAndNamesItsZone) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "DTSTART;TZID=Europe/Berlin:20260804T180000")) << built.Value();
    EXPECT_TRUE(Has(built.Value(), "DTEND;TZID=Europe/Berlin:20260804T190000"));
}

/// ВТОРАЯ ГЛАВНАЯ ПРОВЕРКА ЧАСОВ: то же занятие зимой встаёт на тот же час.
/// Между двумя занятиями лежит перевод часов, и в моменте они отличаются на час
/// — в ленте оба стоят в 18:00, и это то, что человек и обещал ученику.
TEST(ICalendarTest, TheClockChangeDoesNotMoveTheLesson) {
    auto contents = WithOneLesson();
    contents.entries.push_back(CalendarEntry{"lesson-2@pdr",
                                             Utc(2026, 12, 1, 17),
                                             60min,
                                             Berlin(),
                                             "Занятие",
                                             std::string{},
                                             std::string{},
                                             {}});

    const auto built = BuildCalendar(contents);

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "DTSTART;TZID=Europe/Berlin:20260804T180000"));
    EXPECT_TRUE(Has(built.Value(), "DTSTART;TZID=Europe/Berlin:20261201T180000"))
        << "зимнее занятие уехало на час: лента считает моментами вместо часов";
}

/// ЗОНА ОПИСАНА ЯВНЫМИ ПЕРЕХОДАМИ. Без VTIMEZONE календарь не знает, что такое
/// Europe/Berlin, и решает это по-своему — это и есть место, где ошибаются все.
TEST(ICalendarTest, TheZoneIsDescribedByItsRealTransitions) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "BEGIN:VTIMEZONE"));
    EXPECT_TRUE(Has(built.Value(), "TZID:Europe/Berlin"));

    /// Весенний перевод: с +01:00 на +02:00, и момент записан по СТАРОМУ
    /// смещению — так его читает стандарт.
    EXPECT_TRUE(Has(built.Value(), "TZOFFSETFROM:+0100"));
    EXPECT_TRUE(Has(built.Value(), "TZOFFSETTO:+0200"));
    EXPECT_TRUE(Has(built.Value(), "DTSTART:20260329T020000")) << built.Value();

    /// Осенний — обратно, и записан он по летнему смещению.
    EXPECT_TRUE(Has(built.Value(), "DTSTART:20261025T030000"));
    EXPECT_TRUE(Has(built.Value(), "END:VTIMEZONE"));
}

TEST(ICalendarTest, AZoneWithoutClockChangesIsStillDescribed) {
    CalendarFeedContents contents{"Поля", Utc(2026, 7, 1, 12), {}, {}};
    contents.zones.emplace_back(Moscow(), core::ZoneOffsets::Fixed(3h));
    contents.entries.push_back(
        CalendarEntry{"lesson-1@pdr", Utc(2026, 8, 4, 15), 60min, Moscow(), "Занятие", {}, {}, {}});

    const auto built = BuildCalendar(contents);

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "TZID:Europe/Moscow"));
    EXPECT_TRUE(Has(built.Value(), "TZOFFSETTO:+0300"));
    EXPECT_TRUE(Has(built.Value(), "DTSTART;TZID=Europe/Moscow:20260804T180000"));
}

/// СЕРИЯ — ПРАВИЛОМ, ОТМЕНЁННОЕ — СТРОКОЙ EXDATE. Разворачивать серию в ленте
/// значило бы завести второй генератор повторов рядом с тем, которым она
/// хранится.
TEST(ICalendarTest, ASeriesTravelsAsItsRuleAndItsExceptions) {
    CalendarFeedContents contents{"Поля", Utc(2026, 7, 1, 12), {}, {}};
    contents.zones.emplace_back(Berlin(), BerlinRules());
    contents.entries.push_back(CalendarEntry{"series-1@pdr",
                                             Utc(2026, 8, 4, 16),
                                             60min,
                                             Berlin(),
                                             "Занятие",
                                             std::string{},
                                             "FREQ=WEEKLY;INTERVAL=1;BYDAY=TU;COUNT=8",
                                             {Utc(2026, 8, 11, 16), Utc(2026, 8, 18, 16)}});

    const auto built = BuildCalendar(contents);

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=TU;COUNT=8"));
    EXPECT_TRUE(Has(built.Value(), "EXDATE;TZID=Europe/Berlin:20260811T180000,20260818T180000"))
        << built.Value();
}

TEST(ICalendarTest, EveryEventCarriesAnAlarmAndAStableIdentifier) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "UID:lesson-1@pdr"));
    EXPECT_TRUE(Has(built.Value(), "BEGIN:VALARM"));
    EXPECT_TRUE(Has(built.Value(), "TRIGGER:-PT60M"));
    EXPECT_TRUE(Has(built.Value(), "LOCATION:https://example.test/room"));
}

/// СРОК ОБНОВЛЕНИЯ НАЗВАН ДВАЖДЫ, потому что понимают его разные календари.
TEST(ICalendarTest, TheFeedAsksToBeRefreshedNoMoreOftenThanItChanges) {
    const auto built = BuildCalendar(WithOneLesson());

    ASSERT_TRUE(built.HasValue());
    EXPECT_TRUE(Has(built.Value(), "REFRESH-INTERVAL;VALUE=DURATION:PT60M"));
    EXPECT_TRUE(Has(built.Value(), "X-PUBLISHED-TTL:PT60M"));
}

TEST(ICalendarTest, AZoneWithoutRulesIsRefusedRatherThanGuessed) {
    auto contents = WithOneLesson();
    contents.entries.front().zone = Moscow();

    const auto refused = BuildCalendar(contents);

    ASSERT_FALSE(refused.HasValue());
    EXPECT_EQ(refused.Failure().Code(), "calendar_zone_rules_missing");
}

TEST(ICalendarTest, TextIsEscapedTheWayTheStandardWants) {
    EXPECT_EQ(EscapeText("Алгебра, глава 3; повторение"), "Алгебра\\, глава 3\\; повторение");
    EXPECT_EQ(EscapeText("первая\nвторая"), "первая\\nвторая");
    EXPECT_EQ(EscapeText("C:\\путь"), "C:\\\\путь");
}

/// ГЛАВНАЯ ТОНКОСТЬ ПЕРЕНОСА: семьдесят пять ОКТЕТОВ, а не знаков.
TEST(ICalendarTest, LongLinesAreFoldedAtSeventyFiveOctets) {
    const std::string line = "SUMMARY:" + std::string(200, 'a');

    const auto folded = FoldLine(line);

    for (const auto& piece : LinesOf(folded)) {
        EXPECT_LE(piece.size(), 75U) << "строка длиннее семидесяти пяти октетов";
    }
    const auto pieces = LinesOf(folded);
    ASSERT_GT(pieces.size(), 1U);
    for (std::size_t index = 1; index < pieces.size(); ++index) {
        EXPECT_EQ(pieces[index].front(), ' ') << "продолжение не начинается с пробела";
    }
}

/// А ЭТО ТА САМАЯ ОШИБКА, РАДИ КОТОРОЙ ОКТЕТЫ И СЧИТАЮТСЯ: перенос посреди
/// буквы даёт календарю мусор вместо имени.
TEST(ICalendarTest, FoldingNeverCutsALetterInHalf) {
    const std::string line = "SUMMARY:" + [] {
        std::string many;
        for (int index = 0; index < 60; ++index) {
            many += "ё";
        }
        return many;
    }();

    const auto folded = FoldLine(line);

    std::string joined;
    for (const auto& piece : LinesOf(folded)) {
        joined += joined.empty() ? piece : piece.substr(1);
    }
    EXPECT_EQ(joined, line) << "склеенные обратно куски не дают исходной строки";

    for (const auto& piece : LinesOf(folded)) {
        const auto* start = piece.data();
        const auto first =
            static_cast<unsigned char>(*(piece == LinesOf(folded).front() ? start : start + 1));
        EXPECT_NE(first & 0xC0U, 0x80U) << "кусок начинается с продолжающего октета UTF-8";
    }
}

TEST(ICalendarTest, ShortLinesAreLeftAlone) {
    EXPECT_EQ(FoldLine("VERSION:2.0"), "VERSION:2.0");
}

}  // namespace
}  // namespace pdr::scheduling
