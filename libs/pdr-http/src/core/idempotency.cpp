#include "core/idempotency.hpp"

#include <algorithm>
#include <chrono>

namespace pdr::http {
namespace {

bool Printable(char symbol) noexcept {
    return symbol > ' ' && symbol < '\x7f';
}

bool Hex(char symbol) noexcept {
    return (symbol >= '0' && symbol <= '9') || (symbol >= 'a' && symbol <= 'f');
}

constexpr std::size_t kFingerprintLength = 64;

}  // namespace

core::Result<IdempotencyKey> IdempotencyKey::Parse(std::string_view text) {
    if (text.size() < kShortest) {
        return core::Error{core::ErrorKind::kValidation,
                           "idempotency_key_too_short",
                           "короткий ключ столкнётся с чужим в первый же день"};
    }
    if (text.size() > kLongest) {
        return core::Error{
            core::ErrorKind::kValidation, "idempotency_key_too_long", "ключ длиннее допустимого"};
    }
    if (!std::all_of(text.begin(), text.end(), Printable)) {
        return core::Error{core::ErrorKind::kValidation,
                           "idempotency_key_malformed",
                           "ключ приходит от клиента и уходит в журнал: печатаемые знаки без "
                           "пробелов"};
    }
    return IdempotencyKey{std::string{text}};
}

core::Result<RequestFingerprint> RequestFingerprint::Parse(std::string_view text) {
    if (text.size() != kFingerprintLength || !std::all_of(text.begin(), text.end(), Hex)) {
        return core::Error{core::ErrorKind::kValidation,
                           "request_fingerprint_malformed",
                           "отпечаток — шестьдесят четыре знака от 0 до f в нижнем регистре"};
    }
    return RequestFingerprint{std::string{text}};
}

std::string_view Name(Method method) noexcept {
    switch (method) {
        case Method::kGet:
            return "GET";
        case Method::kHead:
            return "HEAD";
        case Method::kOptions:
            return "OPTIONS";
        case Method::kPost:
            return "POST";
        case Method::kPut:
            return "PUT";
        case Method::kPatch:
            return "PATCH";
        case Method::kDelete:
            return "DELETE";
        case Method::kBoundary:
            break;
    }
    return "GET";
}

std::string_view Name(KeyState state) noexcept {
    switch (state) {
        case KeyState::kInProgress:
            return "in_progress";
        case KeyState::kCompleted:
            return "completed";
        case KeyState::kBoundary:
            break;
    }
    return "in_progress";
}

std::optional<KeyState> ParseKeyState(std::string_view text) {
    if (text == Name(KeyState::kInProgress)) {
        return KeyState::kInProgress;
    }
    if (text == Name(KeyState::kCompleted)) {
        return KeyState::kCompleted;
    }
    return std::nullopt;
}

std::string_view Name(ClaimOutcome outcome) noexcept {
    switch (outcome) {
        case ClaimOutcome::kTaken:
            return "taken";
        case ClaimOutcome::kReplay:
            return "replay";
        case ClaimOutcome::kInFlight:
            return "in_flight";
        case ClaimOutcome::kBoundary:
            break;
    }
    return "taken";
}

core::Result<KeyLifetime> KeyLifetime::Compose(int hours) {
    if (hours < kShortestHours || hours > kLongestHours) {
        return core::Error{core::ErrorKind::kValidation,
                           "idempotency_lifetime_out_of_range",
                           "срок жизни ключа вне допустимого"};
    }
    return KeyLifetime{
        std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{hours})};
}

}  // namespace pdr::http
