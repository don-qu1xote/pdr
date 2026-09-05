#pragma once

#include <string_view>

#include <pdr/api/openapi.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/yaml_config/schema.hpp>

#include "application/ports/id_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/operation.hpp"
#include "scheduling/infrastructure/http/parts.hpp"

namespace pdr::scheduling::http {

/// ЗАВЕСТИ РЕГУЛЯРНЫЕ ЗАНЯТИЯ. Права те же, что у одиночной записи: серия — это
/// запись, повторённая правилом, и спрашивать за неё что-то другое значило бы
/// завести обход одиночной проверки.
class CreateSeriesHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::NewSeries,
                                                     api::Series> {
public:
    explicit CreateSeriesHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::NewSeries& body) const override;

    core::Result<api::Series> Run(const Call& call) const override;

    const application::ports::IdGenerator& ids_;
};

class CreateSeriesOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-create-series";

    CreateSeriesOperation(const userver::components::ComponentConfig& config,
                          const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    CreateSeriesHandler handler_;
};

}  // namespace pdr::scheduling::http
