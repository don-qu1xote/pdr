#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace pdr::core {

/// Момент времени в UTC — микросекунды от эпохи Unix.
///
/// Это единственный способ хранить время в домене. Локального времени без зоны
/// не существует: «в среду в 17:00» — не момент, а момент плюс зона, и зона
/// живёт отдельным типом TimeZone. Instant и TimeZone намеренно не складываются
/// друг с другом: перевод в местное время — дело показа, а не домена.
///
/// Часы здесь тоже нет: получить «сейчас» можно только через порт
/// application::ports::Clock, иначе тест расписания начинает зависеть от того,
/// в какую секунду его запустили.
///
/// Арифметика не проверяет переполнение, и это осознанно: диапазона int64
/// микросекунд хватает на ±292 тысячи лет, а занятия столько не длятся.
class Instant final {
public:
    using Duration = std::chrono::microseconds;

    static Instant FromUnixMicros(std::int64_t micros) noexcept;

    std::int64_t UnixMicros() const noexcept {
        return micros_;
    }

    friend Instant operator+(const Instant& instant, Duration delta) noexcept;
    friend Instant operator-(const Instant& instant, Duration delta) noexcept;
    friend Duration operator-(const Instant& later, const Instant& earlier) noexcept;

    friend bool operator==(const Instant&, const Instant&) = default;
    friend std::strong_ordering operator<=>(const Instant&, const Instant&) = default;

private:
    explicit Instant(std::int64_t micros) noexcept : micros_{micros} {}

    std::int64_t micros_{0};
};

/// Зона по базе IANA: «Europe/Moscow», «Asia/Novosibirsk», «UTC».
///
/// Проверяется форма имени, а не существование зоны в базе: база живёт в
/// системе, а домен собирается без неё. Сверку с настоящей базой делает
/// адаптер, там же, где перевод в местное время.
class TimeZone final {
public:
    static std::optional<TimeZone> Parse(std::string_view name);

    const std::string& Name() const noexcept {
        return name_;
    }

    friend bool operator==(const TimeZone&, const TimeZone&) = default;

private:
    explicit TimeZone(std::string name) noexcept : name_{std::move(name)} {}

    std::string name_;
};

}  // namespace pdr::core
