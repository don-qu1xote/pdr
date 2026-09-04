#include "scheduling_service/readiness_handler.hpp"

#include <cstdint>
#include <exception>
#include <string>

#include <pdr/api/openapi.hpp>
#include <pdr/sql_queries.hpp>

#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/http/content_type.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::scheduling_service {
ReadinessHandler::ReadinessHandler(const userver::components::ComponentConfig& config,
                                   const userver::components::ComponentContext& context)
    : HttpHandlerBase{config, context},
      access_{
          context.FindComponent<userver::components::Postgres>(config["cluster"].As<std::string>())
              .GetCluster(),
          infrastructure::db::UnscopedReason::kReadinessProbe},
      alerts_{
          context.FindComponent<userver::components::StatisticsStorage>().GetMetricsStorageRef()} {}

std::string ReadinessHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    static_cast<void>(context);

    request.GetHttpResponse().SetContentType(userver::http::content_type::kApplicationJson);

    api::Readiness answer{};

    try {
        const auto applied =
            access_.Execute(sql::kReadinessAppliedMigrations).AsSingleRow<std::int64_t>();
        answer.ready = applied > 0;
        answer.migrations = static_cast<int>(applied);
        alerts_.Clear(infrastructure::observe::ServiceAlert::kStorageUnreachable);
        if (applied == 0) {
            request.SetResponseStatus(userver::server::http::HttpStatus::kServiceUnavailable);
            answer.why = "схема пуста: миграции не применены";
            alerts_.Raise(infrastructure::observe::ServiceAlert::kMigrationsNotApplied);
        } else {
            alerts_.Clear(infrastructure::observe::ServiceAlert::kMigrationsNotApplied);
        }
    } catch (const std::exception& unreachable) {
        request.SetResponseStatus(userver::server::http::HttpStatus::kServiceUnavailable);
        answer.ready = false;
        answer.why = "хранилище недоступно";
        alerts_.Raise(infrastructure::observe::ServiceAlert::kStorageUnreachable);
        LOG_WARNING() << "готовность: база не отвечает"
                      << userver::logging::LogExtra{{{infrastructure::observe::kStorageFailureField,
                                                      std::string{unreachable.what()}}}};
    }

    return ToJsonString(answer);
}

userver::yaml_config::Schema ReadinessHandler::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::server::handlers::HttpHandlerBase>(R"(
type: object
description: Готовность принимать трафик — база доступна и схема применена
additionalProperties: false
properties:
    cluster:
        type: string
        description: имя компонента базы
)");
}

}  // namespace pdr::scheduling_service
