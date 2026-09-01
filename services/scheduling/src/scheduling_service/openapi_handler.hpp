#pragma once

#include <string>
#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/http/openapi_document.hpp"

namespace pdr::scheduling_service {

/// Спецификация, которую отдают инструментам.
///
/// СПЕЦИФИКАЦИЯ НЕ СОБИРАЕТСЯ ИЗ КОДА. Ручка читает тот же файл, который человек
/// правит руками, и меняет только запись — YAML на JSON. Обратное направление
/// сделало бы источником правды реализацию: «мы поменяли ответ» перестало бы
/// отличаться от «мы поменяли контракт», и клиент узнавал бы о смене контракта
/// из выкатки.
///
/// Файл читается один раз при старте и падает на месте, если не читается: ручка,
/// отдающая пустоту в тот час, когда клиент порождает типы, хуже отсутствующей.
class OpenApiHandler final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-openapi";

    OpenApiHandler(const userver::components::ComponentConfig& config,
                   const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context) const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    infrastructure::http::OpenApiDocument document_;
};

}  // namespace pdr::scheduling_service
