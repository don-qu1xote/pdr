#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dist_lock/dist_locked_worker.hpp>
#include <userver/rcu/rcu.hpp>
#include <userver/testsuite/tasks.hpp>
#include <userver/tracing/span.hpp>
#include <userver/utils/periodic_task.hpp>
#include <userver/utils/statistics/entry.hpp>
#include <userver/utils/statistics/histogram.hpp>
#include <userver/utils/statistics/rate_counter.hpp>
#include <userver/utils/statistics/writer.hpp>
#include <userver/yaml_config/schema.hpp>

#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/db/unscoped_access.hpp"
#include "infrastructure/observe/service_alerts.hpp"
#include "infrastructure/postgres_tenant_aware_repository.hpp"
#include "infrastructure/userver_clock.hpp"
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
    /// Границы корзин длительности прогона в миллисекундах: от «уложился в
    /// пять» до «шёл полминуты». Верхняя корзина у гистограммы бесконечная и
    /// объявления не требует.
    static constexpr std::array<double, 9> kDurationBoundsMs{
        5.0, 10.0, 50.0, 100.0, 500.0, 1000.0, 5000.0, 15000.0, 30000.0};

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

    /// Блокировка, которая всегда при себе.
    ///
    /// Нужна ровно контуру: под ним процесс один, распределять нечего, а прогон
    /// обязан случиться по требованию и сразу. Что блокировка работает,
    /// проверяется на живой базе двумя процессами (scripts/check_jobs.py), и
    /// подменять ту проверку этой было бы подменой предмета.
    class LockOfTestsuite final : public ports::JobLock {
    public:
        bool IsHeld() const override {
            return true;
        }
    };

    struct Watched final {
        std::optional<RunRecord> last;
        bool enabled{false};
    };

    /// ЧТО НАКОПИЛОСЬ ЗА ВСЕ ПРОГОНЫ, а не что было в последнем.
    ///
    /// Последний прогон — это `Watched`: он отвечает на «работает ли сейчас».
    /// На «сколько всего и как долго» он не отвечает вовсе, и складывать одно с
    /// другим нельзя: значение последнего прогона падает до нуля, как только
    /// прогон отработал вхолостую, и график выглядит как остановка.
    ///
    /// Типы — штатные и ровно те, что подходят: `Rate` для счётчиков (читатель
    /// метрик знает, что их складывают и что они не убывают) и `Histogram` для
    /// длительности. Гистограмма, а не среднее: среднее по прогонам, из которых
    /// один занял минуту, а девяносто девять — миллисекунду, не описывает ни
    /// один из ста (PDR-OBS-01). Свои корзины считать не нужно — границы
    /// объявляются, всё остальное делает userver.
    struct Runs final {
        userver::utils::statistics::RateCounter produced;
        userver::utils::statistics::RateCounter repeated;
        std::array<userver::utils::statistics::RateCounter, 3> outcome;
        userver::utils::statistics::Histogram duration{kDurationBoundsMs};
    };

    /// Один прогон по требованию контура. Ставит след и производит действие —
    /// ровно то же, что делает воркер, только без ожидания периода.
    void RunOnce();

    void Work();
    void Watch();

    /// Учесть прогон: теги спана и накопленные метрики за один вызов, чтобы
    /// рабочий путь и путь по требованию не разошлись в том, что записывают.
    void Account(userver::tracing::Span& span, const RunRecord& record);

    void DumpMetrics(userver::utils::statistics::Writer& writer) const;

    PeriodicJob& work_;
    JobName job_;
    infrastructure::UserverClock clock_;
    DynamicConfigJobSettings settings_;
    infrastructure::db::UnscopedAccess unscoped_;
    infrastructure::db::TenantContext tenants_;
    infrastructure::PostgresTenantAwareRepository storage_;
    PostgresJobLedger ledger_;
    PostgresJobJournal journal_;
    RunPeriodicJob runner_;
    userver::rcu::Variable<Watched> watched_;
    std::unique_ptr<userver::dist_lock::DistLockedWorker> worker_;
    std::optional<LockOfWorker> lock_;
    LockOfTestsuite by_request_;
    userver::testsuite::TestsuiteTasks& tasks_;
    userver::utils::PeriodicTask watch_;
    ::pdr::infrastructure::observe::ServiceAlerts alerts_;
    Runs runs_;
    userver::utils::statistics::Entry statistics_;
};

}  // namespace pdr::jobs
