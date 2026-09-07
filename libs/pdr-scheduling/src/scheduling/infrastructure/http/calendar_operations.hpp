#pragma once

#include <string>
#include <string_view>

#include <pdr/api/openapi.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/yaml_config/schema.hpp>

#include "application/ports/secret_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/operation.hpp"
#include "scheduling/infrastructure/http/parts.hpp"

namespace pdr::scheduling::http {

/// ЛЕНТА ПОДПИСКИ. Единственная ручка дерева, отвечающая не JSON.
///
/// `text/calendar` — не наше желание, а условие: лентой её сделает только этот
/// тип содержимого, и без него ни Google, ни Outlook, ни Apple её не прочтут.
/// Форму это не меняет — отказ по-прежнему problem+json и по-прежнему
/// собирается в одном месте.
///
/// УДОСТОВЕРЕНИЕ У НЕЁ В АДРЕСЕ, и опознаёт его `FeedCallersComponent`. Ручка
/// об этом не знает: кого пускать, решает форма, а не она.
class CalendarFeedHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Nothing,
                                                     std::string> {
public:
    explicit CalendarFeedHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Nothing& body) const override;

    std::string_view MediaType() const override;

    core::Result<std::string> Run(const Call& call) const override;

    const application::ports::Digests& digests_;
    CalendarWording wording_;
};

/// ВЫДАТЬ ССЫЛКУ — ОН ЖЕ ПЕРЕВЫПУСТИТЬ ЕЁ. Одна кнопка на оба случая: утёкшую
/// ссылку чинят тем же действием, которым её заводили.
class IssueCalendarFeedHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Nothing,
                                                     api::CalendarSubscription> {
public:
    explicit IssueCalendarFeedHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Nothing& body) const override;

    core::Result<api::CalendarSubscription> Run(const Call& call) const override;

    const application::ports::SecretGenerator& secrets_;
    const application::ports::Digests& digests_;
};

class CalendarFeedOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-calendar-feed";

    CalendarFeedOperation(const userver::components::ComponentConfig& config,
                          const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    CalendarFeedHandler handler_;
};

class IssueCalendarFeedOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-issue-calendar-feed";

    IssueCalendarFeedOperation(const userver::components::ComponentConfig& config,
                               const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    IssueCalendarFeedHandler handler_;
};

}  // namespace pdr::scheduling::http
