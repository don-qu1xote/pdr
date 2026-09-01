#include "scheduling_service/health_handler.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/http/content_type.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_response.hpp>

namespace pdr::scheduling_service {

std::string HealthHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    static_cast<void>(context);

    request.GetHttpResponse().SetContentType(userver::http::content_type::kApplicationJson);

    userver::formats::json::ValueBuilder answer{userver::formats::json::Type::kObject};
    answer["alive"] = true;
    return userver::formats::json::ToString(answer.ExtractValue());
}

}  // namespace pdr::scheduling_service
