#pragma once

#include <memory>
#include <optional>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dist_lock/dist_locked_worker.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/utils/statistics/entry.hpp>
#include <userver/utils/statistics/writer.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/postgres_tenant_aware_repository.hpp"
#include "infrastructure/system_clock.hpp"
#include "jobs/application/ports/job_lock.hpp"
#include "jobs/application/run_periodic_job.hpp"
#include "jobs/contract.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"
#include "jobs/infrastructure/dynamic_config_job_settings.hpp"
#include "jobs/infrastructure/postgres_job_journal.hpp"
#include "jobs/infrastructure/postgres_job_ledger.hpp"

namespace pdr::jobs {

/// Одиночное периодическое задание в сервисе: штатный DistLock плюс прогон.
///
/// Наследник передаёт своё задание и больше ничего не делает: ни блокировку, ни
/// журнал, ни метрику ему трогать не нужно.
///
/// Своего механизма блокировки здесь нет ни строчки. Блокировку берёт
/// `dist_lock::DistLockedWorker` со стратегией `storages::postgres::DistLockStrategy`
/// — она продлевает строку в `jobs_lock`, а потерявшему её воркеру отменяет
/// задачу. Самодельная блокировка «update ... set locked = true» ломается на
/// перезапуске: строка остаётся занятой умершим воркером, и снять её некому.
/// Почему обёртка всё-таки своя, а не `storages::postgres::DistLockComponentBase`,
/// — docs/adr/0011-single-jobs-on-distlock.md: та берёт имя блокировки и её срок
/// из статического конфига, а `PDR-DB-03` требует динамического.
///
/// Два независимых занятия внутри:
///
/// * рабочее — под блокировкой, поэтому идёт ровно на одном процессе кластера;
/// * наблюдающее — штатный `utils::PeriodicTask` на КАЖДОМ процессе: он
///   перечитывает настройки, передаёт их воркеру и обновляет метрику. Иначе
///   процесс, не держащий блокировку, не знал бы о задании ничего и не мог бы
///   показать, что оно молчит сутки.
class PeriodicJobComponentBase : public userver::components::ComponentBase {
public:
    PeriodicJobComponentBase(const userver::components::ComponentConfig& config,
                             const userver::components::ComponentContext& context,
                             PeriodicJob& work);

    ~PeriodicJobComponentBase() override;

    void OnAllComponentsLoaded() override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    class LockOfWorker final : public ports::JobLock {
    public:
        explicit LockOfWorker(const userver::dist_lock::DistLockedWorker& worker) noexcept;

        bool IsHeld() const override;

    private:
        const userver::dist_lock::DistLockedWorker& worker_;
    };

    struct Watched final {
        std::optional<RunRecord> last;
        bool enabled{false};
    };

    void Work();
    void Watch();
    void DumpMetrics(userver::utils::statistics::Writer& writer) const;

    PeriodicJob& work_;
    JobName job_;
    infrastructure::SystemClock clock_;
    DynamicConfigJobSettings settings_;
    userver::storages::postgres::ClusterPtr cluster_;
    infrastructure::PostgresTenantAwareRepository storage_;
    PostgresJobLedger ledger_;
    PostgresJobJournal journal_;
    RunPeriodicJob runner_;
    userver::rcu::Variable<Watched> watched_;
    std::unique_ptr<userver::dist_lock::DistLockedWorker> worker_;
    std::optional<LockOfWorker> lock_;
    userver::utils::PeriodicTask watch_;
    userver::utils::statistics::Entry statistics_;
};

}  // namespace pdr::jobs
