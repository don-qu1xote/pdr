#pragma once

#include <string>
#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/db/unscoped_access.hpp"

namespace pdr::scheduling_service {

/// «Готов принимать трафик»: база доступна и схема не отстала от файлов.
///
/// Второй вопрос — не педантизм. Процесс, поднявшийся на схеме, отставшей на
/// миграцию, отвечает на запросы и молча делает не то: колонки, которой он
/// ждёт, в базе ещё нет. Пускать на него трафик хуже, чем не пускать.
///
/// Живость при этом остаётся соседней ручкой: недоступная база — повод не
/// давать трафик, а не повод перезапускать процесс.
class ReadinessHandler final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "handler-readiness";

    ReadinessHandler(const userver::components::ComponentConfig& config,
                     const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context) const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    infrastructure::db::UnscopedAccess access_;
};

}  // namespace pdr::scheduling_service
