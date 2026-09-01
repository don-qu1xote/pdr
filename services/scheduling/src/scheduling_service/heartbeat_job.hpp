#pragma once

#include <string_view>
#include <vector>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/concurrent/background_task_storage.hpp>
#include <userver/os_signals/subscriber.hpp>

#include "jobs/contract.hpp"
#include "jobs/infrastructure/periodic_job_component.hpp"

namespace pdr::scheduling_service {

/// ПЕРВОЕ НАСТОЯЩЕЕ ПЕРИОДИЧЕСКОЕ ЗАДАНИЕ. Нарочно тривиальное.
///
/// Механизм одиночных заданий написан в PDR-DB-03 и до сих пор ни разу не
/// работал: блокировка, журнал прогонов и следы действий проверялись на живой
/// базе запросами, а не воркером. Задание здесь — не работа, а доказательство,
/// что механизм заводится в процессе: план, след, отметка в журнале.
///
/// Доменной работы расписания в нём нет и не будет — она в PDR-SCHED-12.
///
/// Ключ действия — час суток, а не «сейчас»: ключ, в который подмешано время
/// прогона, новый на каждом просыпании, и идемпотентности не будет вовсе.
class HeartbeatJob final : public jobs::PeriodicJobComponentBase, private jobs::PeriodicJob {
public:
    static constexpr std::string_view kName = "heartbeat-job";

    HeartbeatJob(const userver::components::ComponentConfig& config,
                 const userver::components::ComponentContext& context);

    ~HeartbeatJob() override;

private:
    std::vector<jobs::WorkItem> Plan(core::Instant now) override;

    void Perform(const jobs::WorkItem& item) override;

    /// Мягкая остановка: новую фоновую работу больше не заводим, начатую
    /// дорабатываем. Задача, запущенная и забытая, при остановке процесса
    /// обращается к уже разрушенным зависимостям.
    void OnStopping();

    /// Ставится ПОСЛЕ всего, чем пользуются фоновые задачи: поля разрушаются в
    /// обратном порядке, поэтому хранилище отменит и дождётся их первым.
    userver::os_signals::Subscriber signals_;
    userver::concurrent::BackgroundTaskStorage tasks_;
};

}  // namespace pdr::scheduling_service
