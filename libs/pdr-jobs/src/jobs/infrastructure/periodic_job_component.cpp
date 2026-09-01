#include "jobs/infrastructure/periodic_job_component.hpp"

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include <userver/components/component.hpp>
#include <userver/components/statistics_storage.hpp>
#include <userver/dist_lock/dist_lock_settings.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/engine/sleep.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/postgres/component.hpp>
#include <userver/storages/postgres/dist_lock_strategy.hpp>
#include <userver/testsuite/testsuite_support.hpp>
#include <userver/tracing/span.hpp>
#include <userver/utils/statistics/labels.hpp>
#include <userver/utils/statistics/storage.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

namespace pdr::jobs {
namespace {

/// Срок жизни блокировки равен времени, отведённому на прогон: прогон, не
/// уложившийся в него, прекращается сам, и блокировка честно уходит к другому.
/// Продлевать её и пытаться взять — раз в десятую часть срока, как это делает
/// штатный storages::postgres::DistLockComponentBase.
userver::dist_lock::DistLockSettings AsLockSettings(const JobSettings& settings) {
    const auto attempt = std::chrono::duration_cast<std::chrono::milliseconds>(settings.Attempt());
    const auto tenth = attempt / 10;

    userver::dist_lock::DistLockSettings result{};
    result.lock_ttl = attempt;
    result.acquire_interval = tenth;
    result.prolong_interval = tenth;
    result.forced_stop_margin = tenth;
    result.worker_func_restart_delay =
        std::chrono::duration_cast<std::chrono::milliseconds>(settings.Period());
    result.is_enabled = settings.Enabled();
    return result;
}

JobName NameOf(const userver::components::ComponentConfig& config) {
    auto name = JobName::Parse(config.Name());
    if (!name.has_value()) {
        throw std::runtime_error{"jobs: имя компонента «" + config.Name() +
                                 "» не годится в имя задания: строчные буквы, цифры и "
                                 "разделители . - _"};
    }
    return std::move(*name);
}

JobSettings SettingsOf(const ports::JobSettingsSource& source, const JobName& job) {
    auto settings = source.For(job);
    if (!settings.HasValue()) {
        throw std::runtime_error{"jobs: " + settings.Failure().Detail()};
    }
    return settings.Value();
}

}  // namespace

PeriodicJobComponentBase::LockOfWorker::LockOfWorker(
    const userver::dist_lock::DistLockedWorker& worker) noexcept
    : worker_{worker} {}

bool PeriodicJobComponentBase::LockOfWorker::IsHeld() const {
    return worker_.OwnsLock() && !worker_.IsCancelAdvised();
}

PeriodicJobComponentBase::PeriodicJobComponentBase(
    const userver::components::ComponentConfig& config,
    const userver::components::ComponentContext& context,
    PeriodicJob& work)
    : userver::components::ComponentBase{config, context},
      work_{work},
      job_{NameOf(config)},
      settings_{context.FindComponent<userver::components::DynamicConfig>().GetSource()},
      unscoped_{
          context.FindComponent<userver::components::Postgres>(config["cluster"].As<std::string>())
              .GetCluster(),
          infrastructure::db::UnscopedReason::kClusterWideJobLock},
      tenants_{
          context.FindComponent<userver::components::Postgres>(config["cluster"].As<std::string>())
              .GetCluster()},
      storage_{tenants_},
      ledger_{storage_},
      journal_{unscoped_},
      runner_{ledger_, journal_, clock_},
      tasks_{context.FindComponent<userver::components::TestsuiteSupport>().GetTestsuiteTasks()} {
    const auto settings = SettingsOf(settings_, job_);
    const auto lock_settings = AsLockSettings(settings);

    auto strategy = std::make_shared<userver::storages::postgres::DistLockStrategy>(
        unscoped_.Pool(),
        config["table"].As<std::string>(),
        settings.Lock().Value(),
        lock_settings);

    const auto task_processor = config["task-processor"].As<std::optional<std::string>>();
    worker_ = std::make_unique<userver::dist_lock::DistLockedWorker>(
        job_.Value(),
        [this] { Work(); },
        std::move(strategy),
        lock_settings,
        task_processor.has_value() ? &context.GetTaskProcessor(*task_processor) : nullptr);
    lock_.emplace(*worker_);

    statistics_ =
        context.FindComponent<userver::components::StatisticsStorage>().GetStorage().RegisterWriter(
            "jobs",
            [this](userver::utils::statistics::Writer& writer) { DumpMetrics(writer); },
            {{"job", job_.Value()}});
}

PeriodicJobComponentBase::~PeriodicJobComponentBase() {
    if (tasks_.IsEnabled()) {
        tasks_.UnregisterTask(job_.Value());
    }
    statistics_.Unregister();
    watch_.Stop();
    worker_->Stop();
}

void PeriodicJobComponentBase::OnAllComponentsLoaded() {
    Watch();
    const auto settings = SettingsOf(settings_, job_);
    watch_.Start(job_.Value() + "-watch",
                 {std::chrono::duration_cast<std::chrono::milliseconds>(settings.Period()),
                  {userver::utils::PeriodicTask::Flags::kChaotic}},
                 [this] { Watch(); });

    if (tasks_.IsEnabled()) {
        tasks_.RegisterTask(job_.Value(), [this] { RunOnce(); });
        return;
    }

    worker_->Start();
}

/// ПОД КОНТУРОМ ЗАДАНИЕ ХОДИТ ПО ТРЕБОВАНИЮ, А НЕ ПО РАСПИСАНИЮ.
///
/// Воркер, крутящийся сам по себе, выполняет работу ПОСРЕДИ проверки: набор
/// просит прогон, а след по этому ключу уже поставлен — и точка контроля не
/// срабатывает. Тест при этом не «иногда падает», а падает по-разному в разные
/// дни, потому что зависит от того, кто успел первым.
///
/// Поэтому под контуром запускается только наблюдающее занятие (метрики), а
/// рабочее зовёт сам набор. Что блокировка работает и что двое не сделают одно
/// действие дважды, проверяется на живой базе двумя процессами
/// (scripts/check_jobs.py) — это другой вопрос и другая проверка.
void PeriodicJobComponentBase::RunOnce() {
    const auto settings = settings_.For(job_);
    if (!settings.HasValue()) {
        throw std::runtime_error{"jobs: " + settings.Failure().Detail()};
    }

    auto span = userver::tracing::Span::MakeRootSpan(job_.Value());
    const auto record = runner_.Execute(job_, settings.Value(), work_, by_request_);
    span.AddTag("jobs_outcome", std::string{Name(record.Result())});
    span.AddTag("jobs_produced", record.Produced());
    span.AddTag("jobs_repeated", record.Repeated());
}

void PeriodicJobComponentBase::Work() {
    while (!worker_->IsCancelAdvised()) {
        const auto settings = settings_.For(job_);
        if (!settings.HasValue()) {
            throw std::runtime_error{"jobs: " + settings.Failure().Detail()};
        }

        auto span = userver::tracing::Span::MakeRootSpan(job_.Value());
        const auto record = runner_.Execute(job_, settings.Value(), work_, *lock_);
        span.AddTag("jobs_outcome", std::string{Name(record.Result())});
        span.AddTag("jobs_produced", record.Produced());
        span.AddTag("jobs_repeated", record.Repeated());

        userver::engine::InterruptibleSleepFor(settings.Value().Period());
    }
}

void PeriodicJobComponentBase::Watch() {
    const auto settings = settings_.For(job_);
    if (!settings.HasValue()) {
        LOG_ERROR() << "jobs: " << settings.Failure().Detail();
        return;
    }

    worker_->UpdateSettings(AsLockSettings(settings.Value()));
    watch_.SetSettings(
        {std::chrono::duration_cast<std::chrono::milliseconds>(settings.Value().Period()),
         {userver::utils::PeriodicTask::Flags::kChaotic}});

    watched_.Assign(Watched{journal_.Last(job_), settings.Value().Enabled()});
}

void PeriodicJobComponentBase::DumpMetrics(userver::utils::statistics::Writer& writer) const {
    writer["lock"] = *worker_;

    const auto watched = watched_.ReadCopy();
    const auto settings = settings_.For(job_);
    writer["enabled"] = watched.enabled ? 1 : 0;

    if (!watched.last.has_value()) {
        writer["ran"] = 0;
        writer["silent"] = watched.enabled ? 1 : 0;
        return;
    }

    const auto now = clock_.Now();
    writer["ran"] = 1;
    writer["duration-ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(watched.last->Took()).count();
    writer["silence-ms"] =
        std::chrono::duration_cast<std::chrono::milliseconds>(SilenceFor(*watched.last, now))
            .count();
    writer["produced"] = watched.last->Produced();
    writer["repeated"] = watched.last->Repeated();
    writer["silent"] = watched.enabled && settings.HasValue() &&
                               HasFallenSilent(watched.last, now, settings.Value().SilenceAllowed())
                           ? 1
                           : 0;
}

userver::yaml_config::Schema PeriodicJobComponentBase::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<userver::components::ComponentBase>(R"(
type: object
description: |
    Одиночное периодическое задание под штатным DistLock. Имя блокировки, период
    и отведённое на прогон время лежат в динамическом конфиге PDR_PERIODIC_JOBS
    под именем этого компонента; здесь только то, что от развёртки: где база и
    в какой таблице блокировка.
additionalProperties: false
properties:
    cluster:
        type: string
        description: имя компонента базы, в которой лежит jobs_lock
    table:
        type: string
        description: таблица строк блокировки, обычно jobs_lock
    task-processor:
        type: string
        description: на каком процессоре задач крутить прогон
)");
}

}  // namespace pdr::jobs
