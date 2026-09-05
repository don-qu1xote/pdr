#include <userver/components/common_component_list.hpp>
#include <userver/components/common_server_component_list.hpp>
#include <userver/components/component_list.hpp>
#include <userver/server/handlers/ping.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/secdist/component.hpp>
#include <userver/storages/secdist/provider_component.hpp>
#include <userver/utils/daemon_run.hpp>

#include "identity/infrastructure/component/permissions_component.hpp"
#include "identity/infrastructure/http/session_callers.hpp"
#include "identity/infrastructure/http/sign_in_operation.hpp"
#include "infrastructure/db/tenant_context_component.hpp"
#include "infrastructure/http/middlewares/links.hpp"
#include "infrastructure/http/outgoing_component.hpp"
#include "observability/infrastructure/product_events_component.hpp"
#include "scheduling/infrastructure/http/availability_operations.hpp"
#include "scheduling/infrastructure/http/lesson_operations.hpp"
#include "scheduling/infrastructure/http/series_operation.hpp"
#include "scheduling_service/authorized_route.hpp"
#include "scheduling_service/health_handler.hpp"
#include "scheduling_service/heartbeat_job.hpp"
#include "scheduling_service/openapi_handler.hpp"
#include "scheduling_service/readiness_handler.hpp"
#include "scheduling_service/secrets_guard.hpp"

/// ПЕРВЫЙ ПРОЦЕСС ПРОЕКТА.
///
/// НАБОР КОМПОНЕНТОВ ПОЛНЫЙ, А НЕ МИНИМАЛЬНЫЙ. Документация userver называет
/// production-контур перечнем, и взять из него половину значит через полгода
/// добирать вторую половину на живом сервисе. Отсюда два готовых списка:
///
///   CommonComponentList         журнал, конфиги, статистика, os_signals,
///                               клиент HTTP, DNS, системные метрики и
///                               поддержка контура;
///   CommonServerComponentList   сам сервер и служебные ручки (server-monitor,
///                               dynamic-debug-log, log-level, inspect-requests,
///                               jemalloc, dns-client-control), tests-control и
///                               congestion_control.
///
/// Минимальные списки в них уже вложены, и складывать их сверху нельзя:
/// повторный компонент — не предупреждение, а падение при старте.
///
/// Своего в списке — только то, чего у userver нет: дверь к базе, проверка
/// секретов, права, поток продуктовых событий, задание, три ручки состояния и
/// операции расписания.
///
/// ОПЕРАЦИЙ МНОГО, А НАСЛЕДНИК HttpHandlerBase ОДИН. `AuthorizedRoute`
/// регистрируется столько раз, сколько маршрутов, — под разными именами; какую
/// операцию звать, сказано в его статическом конфиге. Второй наследник ловится
/// scripts/check_http_form.py.
///
/// Ручка `handler-ping` остаётся ШТАТНОЙ и на месте: /health и /readiness
/// написаны ПОВЕРХ неё, а не вместо (ADR-0013). Она отвечает балансеру, они —
/// на разные вопросы человека и оркестратора.
int main(int argc, char* argv[]) {
    const auto components =
        userver::components::ComponentList()
            .AppendComponentList(userver::components::CommonComponentList())
            .AppendComponentList(userver::components::CommonServerComponentList())
            .Append<userver::components::Secdist>()
            .Append<userver::components::DefaultSecdistProvider>()
            .Append<userver::components::Postgres>("postgres-pdr")
            .Append<userver::server::handlers::Ping>()

            .Append<pdr::infrastructure::db::TenantContextComponent>()
            .Append<pdr::infrastructure::http::OutgoingCallsComponent>()
            .Append<pdr::infrastructure::http::SecurityHeadersLinkFactory>()
            .Append<pdr::infrastructure::http::RequestIdLinkFactory>()
            .Append<pdr::infrastructure::http::RequestBodyLinkFactory>()
            .Append<pdr::infrastructure::http::IdempotencyKeyLinkFactory>()

            .Append<pdr::identity::PermissionsComponent>()
            .Append<pdr::identity::CallersComponent>()
            .Append<pdr::identity::SignInOperation>()
            .Append<pdr::observability::ProductEventsComponent>()

            .Append<pdr::scheduling::http::GetAvailabilityOperation>()
            .Append<pdr::scheduling::http::SetAvailabilityOperation>()
            .Append<pdr::scheduling::http::ListLessonsOperation>()
            .Append<pdr::scheduling::http::CreateLessonOperation>()
            .Append<pdr::scheduling::http::GetLessonOperation>()
            .Append<pdr::scheduling::http::CreateSeriesOperation>()

            .Append<pdr::scheduling_service::SecretsGuard>()
            .Append<pdr::scheduling_service::HeartbeatJob>()
            .Append<pdr::scheduling_service::HealthHandler>()
            .Append<pdr::scheduling_service::ReadinessHandler>()
            .Append<pdr::scheduling_service::OpenApiHandler>()
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-sign-in")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-get-availability")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-set-availability")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-list-lessons")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-create-lesson")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-get-lesson")
            .Append<pdr::scheduling_service::AuthorizedRoute>("handler-create-series");

    return userver::utils::DaemonMain(argc, argv, components);
}
