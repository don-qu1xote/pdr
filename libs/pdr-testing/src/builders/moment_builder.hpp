#pragma once

#include <chrono>
#include <stdexcept>

#include "core/types/time.hpp"

namespace pdr::testing {

/// Билдер момента в UTC: `MomentBuilder{}.Utc(2026, 3, 1).At(18, 30).Build()`.
///
/// Домен хранит момент числом микросекунд, и записать «первое марта, 18:30» в
/// тесте руками — это либо магическое число, либо календарные вычисления в
/// каждом файле. Здесь они один раз и проверены своим unit-тестом.
///
/// Зоны здесь нет намеренно: момент — это UTC, а «18:30 по Москве» собирается
/// показом, а не тестом домена.
class MomentBuilder final {
public:
    MomentBuilder& Utc(int year, unsigned month, unsigned day) {
        const std::chrono::year_month_day date{
            std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
        if (!date.ok()) {
            throw std::logic_error{"MomentBuilder: такой даты не существует"};
        }
        days_ = std::chrono::sys_days{date}.time_since_epoch();
        return *this;
    }

    MomentBuilder& At(unsigned hour, unsigned minute, unsigned second = 0) {
        if (hour > 23 || minute > 59 || second > 59) {
            throw std::logic_error{"MomentBuilder: такого времени на часах не бывает"};
        }
        time_ =
            std::chrono::hours{hour} + std::chrono::minutes{minute} + std::chrono::seconds{second};
        return *this;
    }

    /// Сдвиг от собранного момента: «через сутки», «за час до».
    MomentBuilder& Plus(core::Instant::Duration shift) noexcept {
        shift_ += shift;
        return *this;
    }

    MomentBuilder& Minus(core::Instant::Duration shift) noexcept {
        shift_ -= shift;
        return *this;
    }

    core::Instant Build() const {
        const auto since_epoch =
            std::chrono::duration_cast<core::Instant::Duration>(days_ + time_) + shift_;
        return core::Instant::FromUnixMicros(since_epoch.count());
    }

private:
    std::chrono::days days_{};
    std::chrono::seconds time_{};
    core::Instant::Duration shift_{};
};

}  // namespace pdr::testing
