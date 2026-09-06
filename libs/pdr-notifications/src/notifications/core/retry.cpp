#include "notifications/core/retry.hpp"

namespace pdr::notifications {
namespace {

/// Дальше удваивать бессмысленно: любая задержка сверх этого всё равно упрётся
/// в потолок, а сдвиг на 63 — уже неопределённое поведение.
constexpr int kLongestDoubling = 40;

}  // namespace

RetryPolicy::RetryPolicy(int max_attempts,
                         core::Instant::Duration first_delay,
                         core::Instant::Duration attempt) noexcept
    : max_attempts_{max_attempts}, first_delay_{first_delay}, attempt_{attempt} {}

core::Result<RetryPolicy> RetryPolicy::Compose(int max_attempts,
                                               core::Instant::Duration first_delay,
                                               core::Instant::Duration attempt) {
    if (max_attempts < 1) {
        return core::Error{core::ErrorKind::kValidation,
                           "retry_attempts_not_positive",
                           "очередь без единой попытки не отправляет ничего"};
    }
    if (first_delay <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "retry_delay_not_positive",
                           "повтор без задержки — это тот же самый стук в ту же дверь"};
    }
    if (attempt <= core::Instant::Duration::zero()) {
        return core::Error{core::ErrorKind::kValidation,
                           "retry_attempt_not_positive",
                           "попытка без отведённого времени истекает раньше, чем начинается"};
    }

    return RetryPolicy{max_attempts, first_delay, attempt};
}

core::Instant RetryPolicy::Deadline(core::Instant now) const noexcept {
    return now + attempt_;
}

std::optional<core::Instant> RetryPolicy::Again(int attempts, core::Instant now) const noexcept {
    if (attempts >= max_attempts_) {
        return std::nullopt;
    }

    /// Удвоение на КАЖДУЮ сделанную попытку, кроме первой: после первой неудачи
    /// ждём `first_delay`, после второй — вдвое больше. Счёт с нуля дал бы
    /// половину задержки на первом повторе, и это заметили бы не сразу.
    const int doublings = attempts > 0 ? attempts - 1 : 0;
    if (doublings >= kLongestDoubling) {
        return now + kLongestDelay;
    }

    const auto delay = first_delay_ * (std::int64_t{1} << doublings);
    return now + (delay > kLongestDelay ? kLongestDelay : delay);
}

}  // namespace pdr::notifications
