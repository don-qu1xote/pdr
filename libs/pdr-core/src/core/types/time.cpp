#include "core/types/time.hpp"

#include <cstddef>
#include <utility>

namespace pdr::core {
namespace {

constexpr std::size_t kMaxNameLength = 64;
constexpr std::size_t kMaxSegments = 3;

bool IsLetter(char symbol) noexcept {
    return (symbol >= 'A' && symbol <= 'Z') || (symbol >= 'a' && symbol <= 'z');
}

bool IsAllowedInSegment(char symbol) noexcept {
    const bool digit = symbol >= '0' && symbol <= '9';
    return IsLetter(symbol) || digit || symbol == '_' || symbol == '-' || symbol == '+';
}

bool IsSegment(std::string_view segment) noexcept {
    if (segment.empty() || !IsLetter(segment.front())) {
        return false;
    }
    for (const char symbol : segment) {
        if (!IsAllowedInSegment(symbol)) {
            return false;
        }
    }
    return true;
}

}  // namespace

Instant Instant::FromUnixMicros(std::int64_t micros) noexcept {
    return Instant{micros};
}

Instant operator+(const Instant& instant, Instant::Duration delta) noexcept {
    return Instant{instant.micros_ + delta.count()};
}

Instant operator-(const Instant& instant, Instant::Duration delta) noexcept {
    return Instant{instant.micros_ - delta.count()};
}

Instant::Duration operator-(const Instant& later, const Instant& earlier) noexcept {
    return Instant::Duration{later.micros_ - earlier.micros_};
}

std::optional<TimeZone> TimeZone::Parse(std::string_view name) {
    if (name.empty() || name.size() > kMaxNameLength) {
        return std::nullopt;
    }

    std::size_t segments = 0;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t slash = name.find('/', start);
        const std::size_t end = slash == std::string_view::npos ? name.size() : slash;
        if (!IsSegment(name.substr(start, end - start))) {
            return std::nullopt;
        }
        if (++segments > kMaxSegments) {
            return std::nullopt;
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }

    return TimeZone{std::string{name}};
}

}  // namespace pdr::core
