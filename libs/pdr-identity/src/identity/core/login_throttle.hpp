#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "core/errors.hpp"
#include "core/types/time.hpp"

namespace pdr::identity {

/// По чему считаются попытки. Учётная запись и адрес считаются ОТДЕЛЬНО и оба
/// нужны: порог только по записи не мешает перебирать пароли по всему списку
/// учеников с одной машины, порог только по адресу не мешает подбирать пароль
/// к одной записи из ботнета.
enum class AttemptSubject : std::uint8_t {
    kAccount,
    kAddress,
};

std::string_view Name(AttemptSubject subject) noexcept;

std::optional<AttemptSubject> ParseAttemptSubject(std::string_view text);

/// Сколько неудачных попыток и за какое окно считается перебором.
///
/// Значения приходят из динамического конфига (`PDR_LOGIN_THROTTLE`): порог,
/// который нельзя поднять во время атаки и опустить после неё, — это не защита,
/// а ещё одна выкатка.
class ThrottleLimits final {
public:
    static core::Result<ThrottleLimits> Compose(core::Instant::Duration window,
                                                std::uint32_t per_account,
                                                std::uint32_t per_address);

    core::Instant::Duration Window() const noexcept {
        return window_;
    }

    /// Порог для этого счётчика. Порог по адресу выше порога по записи не
    /// случайно: за одним адресом сидит целый класс из школы, за одной записью —
    /// один человек.
    std::uint32_t For(AttemptSubject subject) const noexcept;

    friend bool operator==(const ThrottleLimits&, const ThrottleLimits&) = default;

private:
    ThrottleLimits(core::Instant::Duration window,
                   std::uint32_t per_account,
                   std::uint32_t per_address) noexcept
        : window_{window}, per_account_{per_account}, per_address_{per_address} {}

    core::Instant::Duration window_;
    std::uint32_t per_account_;
    std::uint32_t per_address_;
};

/// Окно попыток: когда началось и сколько неудач в нём накопилось.
///
/// СЧЁТЧИК ЖИВЁТ В БАЗЕ, А НЕ В ПАМЯТИ ПРОЦЕССА. В памяти он считает попытки
/// одной реплики; со второй репликой порог молча удваивается, а перезапуск
/// обнуляет его целиком — то есть защита выключается тем же способом, каким
/// её обходят. Здесь только правило, а хранение — у порта `ports::LoginAttempts`.
///
/// Окно скользит не по каждой попытке, а по первой: иначе попытка раз в минуту
/// продлевала бы запрет вечно, и человек, вспомнивший свой пароль, не смог бы
/// войти никогда.
class AttemptWindow final {
public:
    static AttemptWindow Restore(core::Instant started_at, std::uint32_t attempts) noexcept;

    core::Instant StartedAt() const noexcept {
        return started_at_;
    }
    std::uint32_t Attempts() const noexcept {
        return attempts_;
    }

    /// Записать ещё одну неудачу. Если прежнее окно к этому моменту истекло,
    /// счёт начинается заново — запрет снимается сам, без чьего-либо участия.
    AttemptWindow Registered(core::Instant at, core::Instant::Duration window) const noexcept;

    /// Заперто ли сейчас. Истёкшее окно не запирает, сколько бы в нём ни
    /// накопилось: это и есть «снимается по истечении».
    bool IsBlockedAt(core::Instant moment,
                     core::Instant::Duration window,
                     std::uint32_t limit) const noexcept;

    friend bool operator==(const AttemptWindow&, const AttemptWindow&) = default;

private:
    AttemptWindow(core::Instant started_at, std::uint32_t attempts) noexcept
        : started_at_{started_at}, attempts_{attempts} {}

    core::Instant started_at_;
    std::uint32_t attempts_;
};

}  // namespace pdr::identity
