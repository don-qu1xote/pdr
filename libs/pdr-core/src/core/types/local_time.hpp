#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::core {

/// День недели. Числа те же, что у `std::chrono::weekday`: воскресенье — 0.
enum class Weekday : std::uint8_t {
    kSunday,
    kMonday,
    kTuesday,
    kWednesday,
    kThursday,
    kFriday,
    kSaturday,

    /// ГРАНИЦА СПИСКА, а не день недели.
    kBoundary,
};

std::string_view Name(Weekday day) noexcept;

/// Календарная дата без времени и без зоны: «второе марта две тысячи двадцать
/// шестого». Не момент: пока не сказано, в какой зоне, она не превращается ни
/// в какое мгновение.
class Date final {
public:
    static Result<Date> Compose(int year, unsigned month, unsigned day);

    int Year() const noexcept {
        return year_;
    }
    unsigned Month() const noexcept {
        return month_;
    }
    unsigned Day() const noexcept {
        return day_;
    }

    Weekday DayOfWeek() const noexcept;

    friend bool operator==(const Date&, const Date&) = default;
    friend std::strong_ordering operator<=>(const Date&, const Date&) = default;

private:
    Date(int year, unsigned month, unsigned day) noexcept : year_{year}, month_{month}, day_{day} {}

    int year_{};
    unsigned month_{};
    unsigned day_{};
};

/// Время на часах: часы и минуты. Секунд нет — занятия не назначают на 18:30:17,
/// а лишняя точность требует решать, что с ней делать при сравнении.
class LocalTime final {
public:
    static Result<LocalTime> Compose(unsigned hour, unsigned minute);

    unsigned Hour() const noexcept {
        return hour_;
    }
    unsigned Minute() const noexcept {
        return minute_;
    }

    std::chrono::minutes SinceMidnight() const noexcept {
        return std::chrono::hours{hour_} + std::chrono::minutes{minute_};
    }

    friend bool operator==(const LocalTime&, const LocalTime&) = default;
    friend std::strong_ordering operator<=>(const LocalTime&, const LocalTime&) = default;

private:
    LocalTime(unsigned hour, unsigned minute) noexcept : hour_{hour}, minute_{minute} {}

    unsigned hour_{};
    unsigned minute_{};
};

/// Год-месяц-день-час-минута БЕЗ зоны. Именно то, что человек пишет в
/// расписании и что само по себе не является моментом.
class LocalDateTime final {
public:
    LocalDateTime(Date date, LocalTime time) noexcept : date_{date}, time_{time} {}

    const Date& OnDate() const noexcept {
        return date_;
    }
    const LocalTime& AtTime() const noexcept {
        return time_;
    }

    /// Столько микросекунд от эпохи было бы, если бы эта запись значила UTC.
    ///
    /// Не момент и моментом не станет: величина нужна ровно затем, чтобы из неё
    /// вычесть смещение зоны. Тип поэтому длительность, а не `Instant`.
    Instant::Duration AsIfUtc() const noexcept;

    friend bool operator==(const LocalDateTime&, const LocalDateTime&) = default;
    friend std::strong_ordering operator<=>(const LocalDateTime&, const LocalDateTime&) = default;

private:
    Date date_;
    LocalTime time_;
};

/// Полуинтервал моментов `[from, to)`. Пустых и вывернутых не бывает.
class TimeRange final {
public:
    static Result<TimeRange> Compose(Instant from, Instant to);

    Instant From() const noexcept {
        return from_;
    }
    Instant To() const noexcept {
        return to_;
    }

    Instant::Duration Length() const noexcept {
        return to_ - from_;
    }

    bool Contains(Instant moment) const noexcept {
        return moment >= from_ && moment < to_;
    }

    /// Пересекаются ли отрезки. Касание концами пересечением НЕ считается:
    /// занятие, начинающееся ровно в тот момент, когда кончилось предыдущее,
    /// не конфликтует с ним.
    bool Intersects(const TimeRange& other) const noexcept {
        return from_ < other.to_ && other.from_ < to_;
    }

    friend bool operator==(const TimeRange&, const TimeRange&) = default;

private:
    TimeRange(Instant from, Instant to) noexcept : from_{from}, to_{to} {}

    Instant from_;
    Instant to_;
};

/// Смещение зоны от UTC, действующее с момента `at`.
struct OffsetShift final {
    Instant at;
    Instant::Duration offset;

    friend bool operator==(const OffsetShift&, const OffsetShift&) = default;
};

/// ПРАВИЛА ОДНОЙ ЗОНЫ — ЗНАЧЕНИЕМ, А НЕ ССЫЛКОЙ НА СИСТЕМУ.
///
/// База IANA живёт в системе, а домен собирается без неё
/// (`TimeZone::Parse` проверяет форму имени, а не существование зоны). Поэтому
/// ядро не спрашивает у системы ничего: правила приходят к нему ЗНАЧЕНИЕМ —
/// начальным смещением и упорядоченным списком переводов, — а достаёт их из
/// системы адаптер за портом `application::ports::TimeZoneRules`.
///
/// Так вся тонкая часть календаря — пропавший час весной и повторившийся
/// осенью — проверяется unit-тестами за микросекунды и без единой внешней
/// библиотеки. Зона без переводов (Europe/Moscow после 2014-го) — это просто
/// пустой список.
class ZoneOffsets final {
public:
    /// Переводы обязаны идти по возрастанию момента и не повторять смещение
    /// подряд: список, в котором ничего не меняется, описывает перевод, которого
    /// не было, и разрешать его значило бы разрешить два разных ответа на один
    /// вопрос.
    static Result<ZoneOffsets> Compose(Instant::Duration initial, std::vector<OffsetShift> shifts);

    /// Зона без переводов: одно смещение на всю историю.
    static ZoneOffsets Fixed(Instant::Duration offset);

    Instant::Duration Initial() const noexcept {
        return initial_;
    }

    const std::vector<OffsetShift>& Shifts() const noexcept {
        return shifts_;
    }

    /// Смещение, действующее в этот момент.
    Instant::Duration At(Instant moment) const noexcept;

    friend bool operator==(const ZoneOffsets&, const ZoneOffsets&) = default;

private:
    ZoneOffsets(Instant::Duration initial, std::vector<OffsetShift> shifts) noexcept
        : initial_{initial}, shifts_{std::move(shifts)} {}

    Instant::Duration initial_{};
    std::vector<OffsetShift> shifts_;
};

/// ЧТО ПОЛУЧИЛОСЬ ИЗ МЕСТНОГО ВРЕМЕНИ И ЗОНЫ.
///
/// СЛОЖЕНИЕ МЕСТНОГО ВРЕМЕНИ И ЗОНЫ МОЖЕТ НЕ ИМЕТЬ РЕЗУЛЬТАТА ИЛИ ИМЕТЬ ДВА.
/// Это не исключительная ситуация и не ошибка ввода, а свойство календаря:
/// весной час пропадает целиком, осенью час случается дважды. Функция,
/// возвращающая просто `Instant`, обязана в эти две ночи соврать — и врёт она
/// молча, потому что тип не даёт вызывающему повода спросить.
///
/// Поэтому возвращается ЭТО, и `Kind` обязателен к разбору.
struct ResolveResult final {
    enum class Kind : std::uint8_t {
        /// Обычный случай: ровно один момент.
        kUnique,

        /// Такого времени на часах в этот день не было: часы перевели вперёд.
        /// `first` — момент, которым кончился пропавший промежуток, то есть
        /// ближайшее реальное «после». `second` пуст.
        kSkipped,

        /// Такое время было дважды: часы перевели назад. `first` — первое
        /// (ещё по летнему смещению), `second` — второе.
        kAmbiguous,
    };

    Kind kind{Kind::kUnique};
    Instant first;
    std::optional<Instant> second;

    friend bool operator==(const ResolveResult&, const ResolveResult&) = default;
};

std::string_view Name(ResolveResult::Kind kind) noexcept;

/// Местное время плюс правила зоны — момент, моменты или ничего.
///
/// Правила приходят значением, а не именем зоны: ядру неоткуда взять базу
/// IANA. Тот же вызов по имени зоны — у порта `TimeZoneRules`, он достаёт
/// правила и зовёт эту функцию.
ResolveResult Resolve(const LocalDateTime& local, const ZoneOffsets& offsets);

/// Обратный перевод: момент — в местное время зоны. Однозначен всегда, и в этом
/// вся разница с `Resolve`.
LocalDateTime ToLocal(Instant moment, const ZoneOffsets& offsets);

}  // namespace pdr::core
