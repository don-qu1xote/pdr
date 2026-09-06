#pragma once

#include <optional>
#include <utility>

#include "scheduling/application/ports/booking_window_defaults.hpp"
#include "scheduling/application/ports/booking_window_relief.hpp"

namespace pdr::scheduling::testing {

/// Умолчания практики, заданные тестом.
///
/// Пустой набор по умолчанию, а не «сутки»: проверка, которой окна не нужны, не
/// должна получить их случайно и упасть на них через полгода. Кому нужны — тот
/// их и назовёт.
class FakeWindowDefaults final : public ports::BookingWindowDefaults {
public:
    FakeWindowDefaults() = default;

    explicit FakeWindowDefaults(BookingWindows windows) noexcept : windows_{std::move(windows)} {}

    core::Result<BookingWindows> ForPractice(const core::TenantId&) const override {
        if (broken_) {
            return core::Error{
                core::ErrorKind::kValidation, "booking_window_negative", "негодная настройка"};
        }
        return windows_;
    }

    /// Поменять умолчания посреди проверки. Присваиванием их не подменить:
    /// порт запрещает копирование, и правильно делает — сценарий держит ссылку.
    void Say(BookingWindows windows) noexcept {
        windows_ = std::move(windows);
    }

    void Break() noexcept {
        broken_ = true;
    }

private:
    BookingWindows windows_{BookingWindows::Anything()};
    bool broken_{false};
};

/// Послабления в памяти. Ведёт себя как таблица: пара без строки — пустой
/// ответ, вторая выдача той же паре заменяет первую.
class FakeWindowRelief final : public ports::BookingWindowRelief {
public:
    std::optional<BookingWindows> For(const core::TenantId& tenant,
                                      const core::PersonId& tutor,
                                      const core::PersonId& student) const override {
        for (const auto& given : given_) {
            if (given.tenant == tenant && given.tutor == tutor && given.student == student) {
                return given.windows;
            }
        }
        return std::nullopt;
    }

    core::Result<void> Grant(const BookingRelief& relief) override {
        for (auto& given : given_) {
            if (given.tenant == relief.tenant && given.tutor == relief.tutor &&
                given.student == relief.student) {
                given = relief;
                return {};
            }
        }
        given_.push_back(relief);
        return {};
    }

    const std::vector<BookingRelief>& Given() const noexcept {
        return given_;
    }

private:
    std::vector<BookingRelief> given_;
};

}  // namespace pdr::scheduling::testing
