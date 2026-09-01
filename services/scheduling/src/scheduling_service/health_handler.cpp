#include "scheduling_service/health_handler.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace pdr::scheduling_service {

std::string HealthHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    static_cast<void>(request);
    static_cast<void>(context);

    userver::formats::json::ValueBuilder answer{userver::formats::json::Type::kObject};
    answer["alive"] = true;
    return userver::formats::json::ToString(answer.ExtractValue());
}

}  // namespace pdr::scheduling_service
