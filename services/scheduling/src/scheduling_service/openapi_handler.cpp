#include "scheduling_service/openapi_handler.hpp"

#include <stdexcept>
#include <utility>

#include <userver/components/component.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_response.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace pdr::scheduling_service {
namespace {

infrastructure::http::OpenApiDocument Read(const std::string& path) {
    auto document = infrastructure::http::OpenApiDocument::FromFile(path);
    if (!document.HasValue()) {
        throw std::runtime_error{"спецификация не читается: " + document.Failure().Detail()};
    }
    return std::move(document).Value();
}

}  // namespace

OpenApiHandler::OpenApiHandler(const userver::components::ComponentConfig& config,
                               const userver::components::ComponentContext& context)
    : HttpHandlerBase{config, context}, document_{Read(config["document"].As<std::string>())} {}

std::string OpenApiHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    static_cast<void>(context);

    request.GetHttpResponse().SetContentType(userver::http::content_type::kApplicationJson);
    return document_.Json();
}

userver::yaml_config::Schema OpenApiHandler::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::server::handlers::HttpHandlerBase>(R"(
type: object
description: Спецификация внешнего контура, та же, что в docs/api/openapi.yaml
additionalProperties: false
properties:
    document:
        type: string
        description: путь к docs/api/openapi.yaml
)");
}

}  // namespace pdr::scheduling_service
