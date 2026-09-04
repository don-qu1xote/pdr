#include "scheduling_service/authorized_route.hpp"

#include <string>

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

std::string AuthorizedRoute::GetRequestBodyForLogging(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context,
    const std::string& request_body) const {
    static_cast<void>(request);
    static_cast<void>(context);
    return std::to_string(request_body.size()) + " байт не записано";
}

std::string AuthorizedRoute::GetResponseDataForLogging(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context,
    const std::string& response_data) const {
    static_cast<void>(request);
    static_cast<void>(context);
    return std::to_string(response_data.size()) + " байт не записано";
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
