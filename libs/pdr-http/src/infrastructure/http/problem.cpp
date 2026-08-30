#include "infrastructure/http/problem.hpp"

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>

namespace pdr::infrastructure::http {

std::string ProblemType(std::string_view code) {
    return std::string{kProblemTypePrefix} + std::string{code};
}

std::string Render(const Problem& problem) {
    userver::formats::json::ValueBuilder body{userver::formats::json::Type::kObject};
    body["type"] = problem.type;
    body["title"] = problem.title;
    body["status"] = problem.status;
    body["detail"] = problem.detail;
    body["instance"] = problem.instance;
    body["request_id"] = problem.request_id;
    if (problem.field.has_value()) {
        body["field"] = *problem.field;
    }
    return userver::formats::json::ToString(body.ExtractValue());
}

}  // namespace pdr::infrastructure::http
