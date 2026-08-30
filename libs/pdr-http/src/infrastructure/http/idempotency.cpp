#include "infrastructure/http/idempotency.hpp"

namespace pdr::infrastructure::http {

pdr::http::Method Translate(userver::server::http::HttpMethod method) noexcept {
    switch (method) {
        case userver::server::http::HttpMethod::kGet:
            return pdr::http::Method::kGet;
        case userver::server::http::HttpMethod::kHead:
            return pdr::http::Method::kHead;
        case userver::server::http::HttpMethod::kOptions:
            return pdr::http::Method::kOptions;
        case userver::server::http::HttpMethod::kPost:
            return pdr::http::Method::kPost;
        case userver::server::http::HttpMethod::kPut:
            return pdr::http::Method::kPut;
        case userver::server::http::HttpMethod::kPatch:
            return pdr::http::Method::kPatch;
        case userver::server::http::HttpMethod::kDelete:
            return pdr::http::Method::kDelete;
        case userver::server::http::HttpMethod::kConnect:
        case userver::server::http::HttpMethod::kUnknown:
            break;
    }
    return pdr::http::Method::kBoundary;
}

}  // namespace pdr::infrastructure::http
