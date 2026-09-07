#include "scheduling/core/icalendar.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <string>

namespace pdr::scheduling {
namespace {

constexpr std::string_view kCrLf = "\r\n";
constexpr std::size_t kOctetsPerLine = 75;

/// Кто нас породил. Латиницей и без имени продукта: `PRODID` — опознаватель для
/// машины, а «Поля» и «polya» в латинских идентификаторах запрещены словарём
/// (docs/product/glossary.md).
constexpr std::string_view kProductId = "-//PDR//Scheduling//RU";

std::string Number(int value, int width) {
    std::string digits = std::to_string(value < 0 ? -value : value);
    while (static_cast<int>(digits.size()) < width) {
        digits.insert(digits.begin(), '0');
    }
    return digits;
}

/// Местное время без зоны: `20260804T180000`. Именно так стандарт записывает
/// время, у которого зона названа отдельно параметром TZID.
std::string AsLocalStamp(const core::LocalDateTime& local) {
    return Number(local.OnDate().Year(), 4) + Number(static_cast<int>(local.OnDate().Month()), 2) +
           Number(static_cast<int>(local.OnDate().Day()), 2) + "T" +
           Number(static_cast<int>(local.AtTime().Hour()), 2) +
           Number(static_cast<int>(local.AtTime().Minute()), 2) + "00";
}

core::LocalDateTime InOffset(core::Instant moment, core::Instant::Duration offset) {
    return core::ToLocal(moment, core::ZoneOffsets::Fixed(offset));
}

/// Момент в UTC: `20260804T150000Z`. Так записываются DTSTAMP и всё, у чего
/// зоны быть не должно.
std::string AsUtcStamp(core::Instant moment) {
    return AsLocalStamp(InOffset(moment, core::Instant::Duration::zero())) + "Z";
}

/// Смещение зоны так, как его пишет стандарт: `+0200`, `-0330`.
std::string AsOffset(core::Instant::Duration offset) {
    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(offset).count();
    const auto absolute = minutes < 0 ? -minutes : minutes;
    return std::string{minutes < 0 ? '-' : '+'} + Number(static_cast<int>(absolute / 60), 2) +
           Number(static_cast<int>(absolute % 60), 2);
}

/// Имя смещения там, где у зоны нет общепринятого сокращения. Ровно то же
/// делает сама база IANA для зон вроде Europe/Moscow: `+03`.
std::string AsZoneName(core::Instant::Duration offset) {
    const auto written = AsOffset(offset);
    return written.substr(0, 3);
}

void Put(std::string& text, std::string_view line) {
    text += FoldLine(line);
    text += kCrLf;
}

const core::ZoneOffsets* RulesOf(const CalendarFeedContents& contents, const core::TimeZone& zone) {
    for (const auto& [named, offsets] : contents.zones) {
        if (named == zone) {
            return &offsets;
        }
    }
    return nullptr;
}

/// ОПИСАНИЕ ЗОНЫ ЯВНЫМИ ПЕРЕХОДАМИ, А НЕ ПРАВИЛОМ.
///
/// Стандарт разрешает оба способа. Правило (`RRULE` внутри VTIMEZONE) описывает
/// «последнее воскресенье марта» и работает, пока государство не поменяет
/// правило; явные переходы описывают то, что было и будет НА САМОМ ДЕЛЕ, по
/// базе IANA, и не врут задним числом. Россия отменяла переход дважды — этого
/// довода достаточно.
///
/// STANDARD или DAYLIGHT решается сравнением смещений, и это КОСМЕТИКА: момент
/// календарь считает по `TZOFFSETTO`, а не по названию куска. Правила, которое
/// надо было бы продолжать, здесь нет, поэтому и продолжать нечего.
void PutZone(std::string& text,
             const core::TimeZone& zone,
             const core::ZoneOffsets& offsets,
             core::Instant from) {
    Put(text, "BEGIN:VTIMEZONE");
    Put(text, "TZID:" + zone.Name());

    /// Первый кусок — то смещение, которое действует В НАЧАЛЕ горизонта. Без
    /// него календарь не знает, что делать с занятием до первого перевода.
    Put(text, "BEGIN:STANDARD");
    Put(text, "DTSTART:" + AsLocalStamp(InOffset(from, offsets.Initial())));
    Put(text, "TZOFFSETFROM:" + AsOffset(offsets.Initial()));
    Put(text, "TZOFFSETTO:" + AsOffset(offsets.Initial()));
    Put(text, "TZNAME:" + AsZoneName(offsets.Initial()));
    Put(text, "END:STANDARD");

    auto before = offsets.Initial();
    for (const auto& shift : offsets.Shifts()) {
        const bool forward = shift.offset > before;
        const std::string kind = forward ? "DAYLIGHT" : "STANDARD";

        Put(text, "BEGIN:" + kind);
        /// Момент перехода записывается по СТАРОМУ смещению — так его понимает
        /// стандарт: это последнее показание часов перед переводом.
        Put(text, "DTSTART:" + AsLocalStamp(InOffset(shift.at, before)));
        Put(text, "TZOFFSETFROM:" + AsOffset(before));
        Put(text, "TZOFFSETTO:" + AsOffset(shift.offset));
        Put(text, "TZNAME:" + AsZoneName(shift.offset));
        Put(text, "END:" + kind);

        before = shift.offset;
    }

    Put(text, "END:VTIMEZONE");
}

void PutEntry(std::string& text,
              const CalendarEntry& entry,
              const core::ZoneOffsets& offsets,
              core::Instant assembled_at) {
    const auto zone = "TZID=" + entry.zone.Name() + ":";

    Put(text, "BEGIN:VEVENT");
    Put(text, "UID:" + entry.uid);
    Put(text, "DTSTAMP:" + AsUtcStamp(assembled_at));
    Put(text, "DTSTART;" + zone + AsLocalStamp(core::ToLocal(entry.starts_at, offsets)));
    Put(text,
        "DTEND;" + zone + AsLocalStamp(core::ToLocal(entry.starts_at + entry.duration, offsets)));
    Put(text, "SUMMARY:" + EscapeText(entry.summary));
    if (!entry.location.empty()) {
        Put(text, "LOCATION:" + EscapeText(entry.location));
    }
    if (!entry.rrule.empty()) {
        Put(text, "RRULE:" + entry.rrule);
    }
    if (!entry.cancelled.empty()) {
        std::string dates;
        for (const auto& moment : entry.cancelled) {
            if (!dates.empty()) {
                dates += ",";
            }
            dates += AsLocalStamp(core::ToLocal(moment, offsets));
        }
        Put(text, "EXDATE;" + zone + dates);
    }

    Put(text, "BEGIN:VALARM");
    Put(text, "ACTION:DISPLAY");
    Put(text, "DESCRIPTION:" + EscapeText(entry.summary));
    Put(text, "TRIGGER:-PT" + std::to_string(kAlarmMinutesBefore) + "M");
    Put(text, "END:VALARM");

    Put(text, "END:VEVENT");
}

}  // namespace

std::string_view Name(CalendarSide side) noexcept {
    switch (side) {
        case CalendarSide::kTutor:
            return "tutor";
        case CalendarSide::kIncoming:
            return "incoming";
        case CalendarSide::kBoundary:
            break;
    }
    return "incoming";
}

std::string_view Name(CalendarNaming naming) noexcept {
    switch (naming) {
        case CalendarNaming::kWithoutNames:
            return "without_names";
        case CalendarNaming::kWithNames:
            return "with_names";
        case CalendarNaming::kBoundary:
            break;
    }
    return "without_names";
}

std::optional<CalendarNaming> ParseCalendarNaming(std::string_view text) {
    for (const auto naming : {CalendarNaming::kWithoutNames, CalendarNaming::kWithNames}) {
        if (Name(naming) == text) {
            return naming;
        }
    }
    return std::nullopt;
}

std::string EscapeText(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size());
    for (const char symbol : text) {
        switch (symbol) {
            case '\\':
                escaped += "\\\\";
                break;
            case ';':
                escaped += "\\;";
                break;
            case ',':
                escaped += "\\,";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                break;
            default:
                escaped += symbol;
        }
    }
    return escaped;
}

std::string FoldLine(std::string_view line) {
    if (line.size() <= kOctetsPerLine) {
        return std::string{line};
    }

    std::string folded;
    std::size_t taken = 0;
    std::size_t limit = kOctetsPerLine;

    while (taken < line.size()) {
        std::size_t take = std::min(limit, line.size() - taken);

        /// ОКТЕТЫ, А НЕ ЗНАКИ: «Пётр» — восемь октетов, и перенос посреди буквы
        /// даёт календарю мусор. Продолжающие октеты UTF-8 начинаются с 10, и
        /// отступаем назад, пока не встанем на начало буквы.
        while (take > 0 && taken + take < line.size() &&
               (static_cast<unsigned char>(line[taken + take]) & 0xC0U) == 0x80U) {
            --take;
        }
        if (take == 0) {
            take = std::min(limit, line.size() - taken);
        }

        if (taken > 0) {
            folded += kCrLf;
            folded += ' ';
        }
        folded += line.substr(taken, take);
        taken += take;

        /// У продолжающей строки первый октет занят пробелом.
        limit = kOctetsPerLine - 1;
    }

    return folded;
}

core::Result<std::string> BuildCalendar(const CalendarFeedContents& contents) {
    std::string text;

    Put(text, "BEGIN:VCALENDAR");
    Put(text, "VERSION:2.0");
    Put(text, "PRODID:" + std::string{kProductId});
    Put(text, "CALSCALE:GREGORIAN");
    Put(text, "METHOD:PUBLISH");
    Put(text, "NAME:" + EscapeText(contents.name));
    Put(text, "X-WR-CALNAME:" + EscapeText(contents.name));
    Put(text, "REFRESH-INTERVAL;VALUE=DURATION:PT" + std::to_string(kRefreshMinutes) + "M");
    Put(text, "X-PUBLISHED-TTL:PT" + std::to_string(kRefreshMinutes) + "M");

    for (const auto& [zone, offsets] : contents.zones) {
        PutZone(text, zone, offsets, contents.assembled_at);
    }

    for (const auto& entry : contents.entries) {
        const auto* offsets = RulesOf(contents, entry.zone);
        if (offsets == nullptr) {
            return core::Error{
                core::ErrorKind::kValidation,
                "calendar_zone_rules_missing",
                "для зоны " + entry.zone.Name() + " не принесли правил перевода часов"};
        }
        PutEntry(text, entry, *offsets, contents.assembled_at);
    }

    Put(text, "END:VCALENDAR");
    return text;
}

}  // namespace pdr::scheduling
