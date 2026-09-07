#include "scheduling/infrastructure/http/feed_callers.hpp"

#include <string>

#include <userver/components/component.hpp>
#include <userver/storages/postgres/cluster_types.hpp>
#include <userver/storages/postgres/options.hpp>

#include "infrastructure/db/tenant_context_component.hpp"
#include "scheduling/core/calendar_feed.hpp"
#include "scheduling/infrastructure/postgres_calendar_feeds.hpp"

namespace pdr::scheduling::http {
namespace {

/// Один отказ на все случаи: не тот секрет, не тот кабинет, перевыпущенная
/// ссылка. Различать их в ответе значило бы подсказывать тому, кто перебирает.
core::Error Unknown() {
    return core::Error{core::ErrorKind::kNotFound,
                       "calendar_feed_unknown",
                       "эта ссылка на календарь больше не работает: возьмите новую в кабинете"};
}

}  // namespace

FeedCallersComponent::FeedCallersComponent(const userver::components::ComponentConfig& config,
                                           const userver::components::ComponentContext& context)
    : ComponentBase{config, context},
      tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()} {}

infrastructure::http::CredentialSource FeedCallersComponent::Where() const {
    /// Ни cookie, ни заголовка: у подписки нет ни того ни другого, и спрашивать
    /// их значило бы обещать источник, которого не бывает.
    return infrastructure::http::CredentialSource{std::string_view{}, std::string_view{}};
}

core::Result<infrastructure::http::Caller> FeedCallersComponent::Identify(
    std::string_view, std::string_view, const infrastructure::http::PathArguments& path) const {
    const auto tenant = core::TenantId::Parse(path.Of(kTenantArgument));
    if (!tenant.has_value()) {
        return Unknown();
    }

    const auto secret = CalendarFeedSecret::Parse(path.Of(kSecretArgument));
    if (!secret.HasValue()) {
        return Unknown();
    }

    /// Читающая область: подписку здесь ищут, а не правят. Пусть база откажет
    /// тому, кто однажды допишет сюда «отметим последнее обращение».
    auto scope = tenants_.Open(*tenant,
                               userver::storages::postgres::ClusterHostType::kMaster,
                               userver::storages::postgres::TransactionOptions{
                                   userver::storages::postgres::TransactionOptions::kReadOnly});
    PostgresCalendarFeeds feeds{scope};
    const auto feed = feeds.ByDigest(*tenant, digests_.Of(secret.Value().Value()));
    scope.Commit();

    if (!feed.has_value()) {
        return Unknown();
    }

    return infrastructure::http::Caller{*tenant, feed->Person()};
}

}  // namespace pdr::scheduling::http
