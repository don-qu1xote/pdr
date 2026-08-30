#include "infrastructure/http/request_schema.hpp"

#include <fstream>
#include <sstream>
#include <utility>

#include <userver/formats/json/exception.hpp>
#include <userver/formats/json/serialize.hpp>

namespace pdr::infrastructure::http {

RequestSchema::RequestSchema(std::shared_ptr<const userver::formats::json::Schema> schema) noexcept
    : schema_{std::move(schema)} {}

core::Result<RequestSchema> RequestSchema::FromText(std::string_view text) {
    try {
        return RequestSchema{std::make_shared<const userver::formats::json::Schema>(
            userver::formats::json::FromString(text))};
    } catch (const userver::formats::json::Exception& broken) {
        return core::Error{core::ErrorKind::kValidation, "schema_malformed", broken.what()};
    }
}

core::Result<RequestSchema> RequestSchema::FromFile(const std::string& path) {
    std::ifstream file{path};
    if (!file.is_open()) {
        return core::Error{core::ErrorKind::kNotFound, "schema_missing", path};
    }
    std::ostringstream text;
    text << file.rdbuf();
    return FromText(text.str());
}

core::Result<userver::formats::json::Value> RequestSchema::Parse(std::string_view body,
                                                                 std::string& field) const {
    field.clear();

    userver::formats::json::Value parsed;
    try {
        parsed = userver::formats::json::FromString(body);
    } catch (const userver::formats::json::Exception& broken) {
        return core::Error{core::ErrorKind::kValidation, "request_not_json", broken.what()};
    }

    auto checked = schema_->Validate(parsed);
    if (checked.IsValid()) {
        return parsed;
    }

    const auto error = std::move(checked).GetError();
    field = std::string{error.GetValuePath()};
    return core::Error{
        core::ErrorKind::kValidation,
        "request_field_invalid",
        "поле «" + field + "» не проходит схему: " + std::string{error.GetDetailsString()}};
}

}  // namespace pdr::infrastructure::http
