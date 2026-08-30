#include "identity/core/login_throttle.hpp"

#include <string>

namespace pdr::identity {
namespace {

/// Ноль попыток — это «войти нельзя никому», а не «строго»: такой порог
/// запирает продукт целиком, и заметить это можно только по тишине в поддержке.
constexpr std::uint32_t kLeastLimit = 1;

}  // namespace

std::string_view Name(AttemptSubject subject) noexcept {
    switch (subject) {
        case AttemptSubject::kAccount:
            return "account";
        case AttemptSubject::kAddress:
            return "address";
    }
    return "account";
}

std::optional<AttemptSubject> ParseAttemptSubject(std::string_view text) {
    if (text == "account") {
        return AttemptSubject::kAccount;
    }
    if (text == "address") {
        return AttemptSubject::kAddress;
    }
    return std::nullopt;
}

core::Result<ThrottleLimits> ThrottleLimits::Compose(core::Instant::Duration window,
                                                     std::uint32_t per_account,
                                                     std::uint32_t per_address) {
    if (window <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "throttle_window_not_positive",
                           "окно нулевой длины не считает ничего"};
    }
    if (per_account < kLeastLimit || per_address < kLeastLimit) {
        return core::Error{core::ErrorKind::kValidation,
                           "throttle_limit_too_low",
                           "порог меньше одной попытки запирает вход всем"};
    }
    if (per_address < per_account) {
        return core::Error{core::ErrorKind::kValidation,
                           "throttle_address_below_account",
                           "за одним адресом сидит целый класс, за одной записью — один "
                           "человек: порог по адресу не бывает строже"};
    }

    return ThrottleLimits{window, per_account, per_address};
}

std::uint32_t ThrottleLimits::For(AttemptSubject subject) const noexcept {
    switch (subject) {
        case AttemptSubject::kAccount:
            return per_account_;
        case AttemptSubject::kAddress:
            return per_address_;
    }
    return per_account_;
}

AttemptWindow AttemptWindow::Restore(core::Instant started_at, std::uint32_t attempts) noexcept {
    return AttemptWindow{started_at, attempts};
}

AttemptWindow AttemptWindow::Registered(core::Instant at,
                                        core::Instant::Duration window) const noexcept {
    if (at >= started_at_ + window) {
        return AttemptWindow{at, 1};
    }

    return AttemptWindow{started_at_, attempts_ + 1};
}

bool AttemptWindow::IsBlockedAt(core::Instant moment,
                                core::Instant::Duration window,
                                std::uint32_t limit) const noexcept {
    if (moment >= started_at_ + window) {
        return false;
    }
    return attempts_ >= limit;
}

}  // namespace pdr::identity
