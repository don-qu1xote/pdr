#include "infrastructure/http/middlewares/links.hpp"

#include <string>

#include <userver/server/http/http_response.hpp>

#include "core/idempotency.hpp"
#include "infrastructure/http/idempotency.hpp"
#include "infrastructure/http/middlewares/pipeline.hpp"
#include "infrastructure/http/request_id.hpp"
#include "infrastructure/http/security_headers.hpp"

namespace pdr::infrastructure::http {

void SecurityHeadersLink::HandleRequest(userver::server::http::HttpRequest& request,
                                        userver::server::request::RequestContext& context) const {
    ApplySecurityHeaders(request.GetHttpResponse());

    Next(request, context);
}

void RequestIdLink::HandleRequest(userver::server::http::HttpRequest& request,
                                  userver::server::request::RequestContext& context) const {
    auto& prepared = PreparedIn(context);
    prepared.request_id = RequestIdOf(request);
    request.GetHttpResponse().SetHeader(std::string{kRequestIdHeader}, prepared.request_id);

    Next(request, context);
}

void RequestBodyLink::HandleRequest(userver::server::http::HttpRequest& request,
                                    userver::server::request::RequestContext& context) const {
    PreparedIn(context).body = request.RequestBody();

    Next(request, context);
}

void IdempotencyKeyLink::HandleRequest(userver::server::http::HttpRequest& request,
                                       userver::server::request::RequestContext& context) const {
    const auto key =
        pdr::http::IdempotencyKey::Parse(request.GetHeader(std::string{kIdempotencyKeyHeader}));
    if (key.HasValue()) {
        PreparedIn(context).key = key.Value();
    }

    Next(request, context);
}

}  // namespace pdr::infrastructure::http
