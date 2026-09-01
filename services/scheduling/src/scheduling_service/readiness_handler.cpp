#include "scheduling_service/readiness_handler.hpp"

#include <cstdint>
#include <exception>

#include <userver/components/component.hpp>
#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/server/http/http_status.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/query.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace pdr::scheduling_service {
namespace {

/// Реестр применённых миграций. Арендатора у него нет по существу — схема одна
/// на всех, — поэтому читается он второй дверью и с названной причиной.
const userver::storages::postgres::Query kAppliedMigrations{
    "SELECT count(*) FROM schema_version",
    userver::storages::postgres::Query::Name{"readiness_applied_migrations"},
};

}  // namespace

ReadinessHandler::ReadinessHandler(const userver::components::ComponentConfig& config,
                                   const userver::components::ComponentContext& context)
    : HttpHandlerBase{config, context},
      access_{
          context.FindComponent<userver::components::Postgres>(config["cluster"].As<std::string>())
              .GetCluster(),
          infrastructure::db::UnscopedReason::kReadinessProbe} {}

std::string ReadinessHandler::HandleRequestThrow(
    const userver::server::http::HttpRequest& request,
    userver::server::request::RequestContext& context) const {
    static_cast<void>(context);

    userver::formats::json::ValueBuilder answer{userver::formats::json::Type::kObject};

    try {
        const auto applied = access_.Execute(kAppliedMigrations).AsSingleRow<std::int64_t>();
        answer["ready"] = applied > 0;
        answer["migrations"] = applied;
        if (applied == 0) {
            request.SetResponseStatus(userver::server::http::HttpStatus::kServiceUnavailable);
            answer["why"] = "схема пуста: миграции не применены";
        }
    } catch (const std::exception& unreachable) {
        request.SetResponseStatus(userver::server::http::HttpStatus::kServiceUnavailable);
        answer["ready"] = false;
        answer["why"] = "хранилище недоступно";
        LOG_WARNING() << "готовность: база не отвечает: " << unreachable.what();
    }

    return userver::formats::json::ToString(answer.ExtractValue());
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
