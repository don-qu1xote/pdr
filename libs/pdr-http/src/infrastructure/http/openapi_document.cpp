#include "infrastructure/http/openapi_document.hpp"

#include <utility>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/formats/yaml/exception.hpp>
#include <userver/formats/yaml/serialize.hpp>
#include <userver/formats/yaml/value.hpp>

namespace pdr::infrastructure::http {
namespace {

userver::formats::json::Value AsJson(const userver::formats::yaml::Value& node);

userver::formats::json::Value ObjectOf(const userver::formats::yaml::Value& node) {
    userver::formats::json::ValueBuilder built{userver::formats::json::Type::kObject};
    for (auto item = node.begin(); item != node.end(); ++item) {
        built[item.GetName()] = AsJson(*item);
    }
    return built.ExtractValue();
}

userver::formats::json::Value ArrayOf(const userver::formats::yaml::Value& node) {
    userver::formats::json::ValueBuilder built{userver::formats::json::Type::kArray};
    for (const auto& item : node) {
        built.PushBack(AsJson(item));
    }
    return built.ExtractValue();
}

userver::formats::json::Value AsJson(const userver::formats::yaml::Value& node) {
    if (node.IsObject()) {
        return ObjectOf(node);
    }
    if (node.IsArray()) {
        return ArrayOf(node);
    }
    if (node.IsMissing() || node.IsNull()) {
        return userver::formats::json::ValueBuilder{userver::formats::json::Type::kNull}
            .ExtractValue();
    }
    if (node.IsBool()) {
        return userver::formats::json::ValueBuilder{node.As<bool>()}.ExtractValue();
    }
    if (node.IsInt64()) {
        return userver::formats::json::ValueBuilder{node.As<std::int64_t>()}.ExtractValue();
    }
    if (node.IsDouble()) {
        return userver::formats::json::ValueBuilder{node.As<double>()}.ExtractValue();
    }
    return userver::formats::json::ValueBuilder{node.As<std::string>()}.ExtractValue();
}

}  // namespace

core::Result<OpenApiDocument> OpenApiDocument::FromFile(const std::string& path) {
    try {
        return OpenApiDocument{userver::formats::json::ToString(
            AsJson(userver::formats::yaml::blocking::FromFile(path)))};
    } catch (const userver::formats::yaml::Exception& broken) {
        return core::Error{
            core::ErrorKind::kNotFound, "openapi_document_unreadable", path + ": " + broken.what()};
    }
}

}  // namespace pdr::infrastructure::http
