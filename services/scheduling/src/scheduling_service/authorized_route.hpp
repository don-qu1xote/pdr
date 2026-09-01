#pragma once

#include <string>
#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/handlers/http_handler_base.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/http/operation.hpp"

namespace pdr::scheduling_service {

/// ЕДИНСТВЕННЫЙ НАСЛЕДНИК `server::handlers::HttpHandlerBase`, который зовёт
/// форму запроса. Тонкий до предела: находит операцию по имени и отдаёт ей
/// запрос.
///
/// Зачем он вообще нужен, если форма уже есть. Мимо формы проходит всё, что
/// даёт база userver: дедлайны и ответ 498, метрики `handler-*`,
/// `POSTGRES_HANDLERS_COMMAND_CONTROL` (таймауты по имени ручки), выбор
/// процессора задач, `USERVER_LOG_REQUEST` и штатный конвейер звеньев. Ручка,
/// написанная мимо базы, теряет всё это молча.
///
/// Зачем он ОДИН. Второй наследник — это второе место, где решают, что делать
/// с запросом, и они разойдутся: один спросит политику до открытия области,
/// другой после. Маршрутов при этом сколько угодно: класс регистрируется в
/// списке компонентов под разными именами, а какую операцию звать — сказано в
/// его статическом конфиге. Второй наследник ловится
/// `scripts/check_http_form.py`.
class AuthorizedRoute final : public userver::server::handlers::HttpHandlerBase {
public:
    static constexpr std::string_view kName = "pdr-authorized-route";

    AuthorizedRoute(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

    std::string HandleRequestThrow(
        const userver::server::http::HttpRequest& request,
        userver::server::request::RequestContext& context) const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    const infrastructure::http::OperationComponent& operation_;
};

}  // namespace pdr::scheduling_service
