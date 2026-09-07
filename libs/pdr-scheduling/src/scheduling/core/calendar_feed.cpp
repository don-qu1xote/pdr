#include "scheduling/core/calendar_feed.hpp"

#include <algorithm>
#include <chrono>

namespace pdr::scheduling {
namespace {

bool IsBase64Url(char symbol) noexcept {
    return (symbol >= 'A' && symbol <= 'Z') || (symbol >= 'a' && symbol <= 'z') ||
           (symbol >= '0' && symbol <= '9') || symbol == '-' || symbol == '_';
}

}  // namespace

core::Result<CalendarFeedSecret> CalendarFeedSecret::Parse(std::string_view text) {
    if (text.size() < kLeastLength) {
        return core::Error{core::ErrorKind::kValidation,
                           "calendar_secret_too_short",
                           "ссылка на календарь неполная: откройте её из настроек целиком"};
    }
    if (!std::all_of(text.begin(), text.end(), IsBase64Url)) {
        return core::Error{core::ErrorKind::kValidation,
                           "calendar_secret_malformed",
                           "ссылка на календарь испорчена по дороге: скопируйте её заново"};
    }

    return CalendarFeedSecret{std::string{text}};
}

core::Result<CalendarFeed> CalendarFeed::Compose(core::TenantId tenant,
                                                 core::PersonId person,
                                                 core::Digest secret,
                                                 CalendarNaming naming) {
    if (naming == CalendarNaming::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "calendar_naming_unknown",
                           "такого способа называть занятия не бывает"};
    }

    return CalendarFeed{std::move(tenant), std::move(person), std::move(secret), naming};
}

core::TimeRange FeedHorizon(core::Instant now) {
    const auto back =
        std::chrono::duration_cast<core::Instant::Duration>(std::chrono::hours{24 * kFeedPastDays});
    const auto ahead = std::chrono::duration_cast<core::Instant::Duration>(
        std::chrono::hours{24 * kFeedAheadDays});
    return core::TimeRange::Compose(now - back, now + ahead).Value();
}

}  // namespace pdr::scheduling
