#include "notifications/infrastructure/outbox_dispatcher.hpp"

#include <chrono>
#include <cstdint>
#include <string>

#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/tracing/span.hpp>
#include <userver/utils/statistics/storage.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::notifications {
namespace {

namespace fields = ::pdr::infrastructure::observe;

}  // namespace

OutboxDispatcher::OutboxDispatcher(const userver::components::ComponentConfig& config,
                                   const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      settings_{context.FindComponent<userver::components::DynamicConfig>().GetSource()},
      unscoped_{
          context.FindComponent<userver::components::Postgres>(config["cluster"].As<std::string>())
              .GetCluster(),
          infrastructure::db::UnscopedReason::kOutboxDispatch},
      queue_{unscoped_},
      dispatching_{queue_, sender_, settings_, clock_},
      tasks_{context.FindComponent<userver::components::TestsuiteSupport>().GetTestsuiteTasks()} {
    statistics_ =
        context.FindComponent<userver::components::StatisticsStorage>().GetStorage().RegisterWriter(
            "notifications.outbox",
            [this](userver::utils::statistics::Writer& writer) { DumpMetrics(writer); });
}

OutboxDispatcher::~OutboxDispatcher() {
    if (tasks_.IsEnabled()) {
        tasks_.UnregisterTask(std::string{kName});
    }
    statistics_.Unregister();
    pass_.Stop();
}

void OutboxDispatcher::OnAllComponentsLoaded() {
    if (tasks_.IsEnabled()) {
        tasks_.RegisterTask(std::string{kName}, [this] { Pass(); });
        return;
    }

    /// Разброс намеренный (`kChaotic`): реплики, проснувшиеся в одну и ту же
    /// миллисекунду, дерутся за одни и те же строки — `skip locked` их разведёт,
    /// но лишний заход в базу они всё равно сделают.
    pass_.Start(std::string{kName},
                {std::chrono::duration_cast<std::chrono::milliseconds>(settings_.Poll()),
                 {userver::utils::PeriodicTask::Flags::kChaotic}},
                [this] { Pass(); });
}

void OutboxDispatcher::Pass() {
    auto span = userver::tracing::Span::MakeRootSpan(std::string{kName});

    const auto report = dispatching_.Execute(settings_.Batch());
    if (!report.HasValue()) {
        /// Негодные настройки — не повод падать: очередь подождёт, а прежние
        /// значения продолжают действовать до следующей правки.
        passes_.refused.Increment();
        LOG_ERROR() << "разбор очереди не начался"
                    << userver::logging::LogExtra{
                           {{fields::kOutboxFailureField, report.Failure().Code()}}};
        return;
    }

    const auto& pass = report.Value();
    passes_.claimed.Add(userver::utils::statistics::Rate{static_cast<std::uint64_t>(pass.claimed)});
    passes_.sent.Add(userver::utils::statistics::Rate{static_cast<std::uint64_t>(pass.sent)});
    passes_.retried.Add(userver::utils::statistics::Rate{static_cast<std::uint64_t>(pass.retried)});
    passes_.gave_up.Add(userver::utils::statistics::Rate{static_cast<std::uint64_t>(pass.gave_up)});

    span.AddTag(fields::kOutboxClaimedField, static_cast<std::int64_t>(pass.claimed));
    span.AddTag(fields::kOutboxSentField, static_cast<std::int64_t>(pass.sent));
    span.AddTag(fields::kOutboxGaveUpField, static_cast<std::int64_t>(pass.gave_up));

    /// СДАВШАЯСЯ СТРОКА — ЭТО СОБЫТИЕ, А НЕ ЧИСЛО В МЕТРИКЕ. Человеку не
    /// написали, и узнать об этом по графику нельзя: график покажет «ноль», а
    /// не «кому и почему». Запись в журнал — единственное место, где это
    /// написано словами.
    if (pass.gave_up > 0) {
        LOG_ERROR() << "оповещения не ушли: попытки исчерпаны"
                    << userver::logging::LogExtra{
                           {{fields::kOutboxGaveUpField, static_cast<std::int64_t>(pass.gave_up)}}};
    }
}

void OutboxDispatcher::DumpMetrics(userver::utils::statistics::Writer& writer) const {
    writer["claimed"] = passes_.claimed;
    writer["sent"] = passes_.sent;
    writer["retried"] = passes_.retried;
    writer["gave-up"] = passes_.gave_up;
    writer["refused"] = passes_.refused;
}

userver::yaml_config::Schema OutboxDispatcher::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(R"(
type: object
description: отправщик исходящей очереди
additionalProperties: false
properties:
    cluster:
        type: string
        description: компонент подключения к базе
)");
}

}  // namespace pdr::notifications
