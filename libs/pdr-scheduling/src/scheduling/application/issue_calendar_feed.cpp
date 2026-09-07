#include "scheduling/application/issue_calendar_feed.hpp"

namespace pdr::scheduling {

IssueCalendarFeed::IssueCalendarFeed(ports::CalendarFeeds& feeds,
                                     const application::ports::SecretGenerator& secrets,
                                     const application::ports::Digests& digests,
                                     const application::ports::Clock& clock) noexcept
    : feeds_{feeds}, secrets_{secrets}, digests_{digests}, clock_{clock} {}

core::Result<IssueCalendarFeed::Answer> IssueCalendarFeed::Execute(const Request& request) const {
    const auto secret = CalendarFeedSecret::Parse(secrets_.NextText(kSecretBytes));
    if (!secret.HasValue()) {
        return secret.Failure();
    }

    /// Настройка при перевыпуске сохраняется самим запросом: человек чинил
    /// утечку, а не менял способ называть занятия. Умолчание здесь — то, с чем
    /// подписка заводится впервые.
    const auto feed = CalendarFeed::Compose(request.tenant,
                                            request.person,
                                            digests_.Of(secret.Value().Value()),
                                            CalendarNaming::kWithoutNames);
    if (!feed.HasValue()) {
        return feed.Failure();
    }

    const auto issued = feeds_.Issue(feed.Value(), clock_.Now());
    if (!issued.HasValue()) {
        return issued.Failure();
    }

    return Answer{feed.Value(), secret.Value()};
}

}  // namespace pdr::scheduling
