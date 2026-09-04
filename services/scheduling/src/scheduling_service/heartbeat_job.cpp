#include "scheduling_service/heartbeat_job.hpp"

#include <cstdint>
#include <string>

#include <userver/components/component.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/logging/log.hpp>
#include <userver/os_signals/component.hpp>
#include <userver/testsuite/testpoint.hpp>

#include "core/types/time.hpp"
#include "infrastructure/observe/log_fields.hpp"

namespace pdr::scheduling_service {
namespace {

constexpr std::int64_t kMicrosInHour = 3'600'000'000;

}  // namespace

HeartbeatJob::HeartbeatJob(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context)
    : PeriodicJobComponentBase{config, context, *this},
      signals_{context.FindComponent<userver::os_signals::ProcessorComponent>().Get().AddListener(
          this, std::string{kName}, userver::os_signals::kSigUsr1, &HeartbeatJob::OnStopping)} {}

HeartbeatJob::~HeartbeatJob() {
    signals_.Unsubscribe();
}

std::vector<jobs::WorkItem> HeartbeatJob::Plan(core::Instant now) {
    const auto hour = now.UnixMicros() / kMicrosInHour;

    /// Работа кластерная: она про то, что механизм жив, а не про чьи-то данные.
    /// Арендатор у неё системный — так решено в
    /// docs/adr/0021-system-tenant-for-clusterwide-jobs.md, и это не «нулевой
    /// идентификатор на время», а строка в identity_tenant.
    return {jobs::WorkItem{jobs::SystemTenant(), "heartbeat:" + std::to_string(hour)}};
}

void HeartbeatJob::Perform(const jobs::WorkItem& item) {
    const std::string key = item.key;

    /// Оповещение — ФОНОВОЙ РАБОТОЙ, привязанной ко времени жизни компонента.
    /// Оторванная от него задача при остановке процесса обращается к уже
    /// разрушенным зависимостям; здесь хранилище отменит и дождётся её само.
    tasks_.AsyncDetach("heartbeat-noted", [key] {
        LOG_INFO() << "задание отработало"
                   << userver::logging::LogExtra{{{infrastructure::observe::kJobKeyField, key}}};

        /// Точка контроля, а не запись в журнал: набор дожидается ЕЁ, а не
        /// спит (docs/testing.md, scripts/check_testsuite.py).
        TESTPOINT("heartbeat-performed", [&key] {
            userver::formats::json::ValueBuilder said{userver::formats::json::Type::kObject};
            said["key"] = key;
            return said.ExtractValue();
        }());
    });
}

void HeartbeatJob::OnStopping() {
    LOG_INFO() << "останавливаемся: новую фоновую работу не заводим, начатую дорабатываем";
    tasks_.CancelAndWait();
}

}  // namespace pdr::scheduling_service
