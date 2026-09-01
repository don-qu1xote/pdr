#include "scheduling_service/authorized_route.hpp"

#include <userver/components/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "infrastructure/http/middlewares/pipeline.hpp"

namespace pdr::scheduling_service {

AuthorizedRoute::AuthorizedRoute(const userver::components::ComponentConfig& config,
                                 const userver::components::ComponentContext& context)
    : HttpHandlerBase{config, context},
      operation_{context.FindComponent<infrastructure::http::OperationComponent>(
          config["operation"].As<std::string>())} {}

std::string AuthorizedRoute::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    return operation_.Handler().Serve(request, infrastructure::http::PreparedOf(context));
}

userver::yaml_config::Schema AuthorizedRoute::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::server::handlers::HttpHandlerBase>(R"(
type: object
description: Маршрут к операции контекста; сам ничего не решает
additionalProperties: false
properties:
    operation:
        type: string
        description: имя компонента-операции, который обслуживает этот адрес
)");
}

}  // namespace pdr::scheduling_service
