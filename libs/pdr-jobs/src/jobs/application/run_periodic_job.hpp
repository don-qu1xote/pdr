#pragma once

#include "application/ports/clock.hpp"
#include "jobs/application/ports/job_journal.hpp"
#include "jobs/application/ports/job_ledger.hpp"
#include "jobs/application/ports/job_lock.hpp"
#include "jobs/contract.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/job_settings.hpp"
#include "jobs/core/run_record.hpp"

namespace pdr::jobs {

/// Один прогон периодического задания.
///
/// Здесь и живёт вся идемпотентность, и она устроена одинаково для любого
/// задания: сначала след, потом действие. Задание не обязано быть аккуратным —
/// оно обязано выдавать одинаковые ключи на одинаковую работу.
///
/// Прогон прекращается сам в двух случаях: блокировку отобрали или кончилось
/// отведённое время. Оба проверяются между единицами плана, а не «когда-нибудь»:
/// воркер, продолжающий работу без блокировки, — это и есть та самая двойная
/// рассылка, ради невозможности которой всё написано.
class RunPeriodicJob final {
public:
    RunPeriodicJob(ports::JobLedger& ledger,
                   ports::JobJournal& journal,
                   const application::ports::Clock& clock) noexcept;

    RunRecord Execute(const JobName& job,
                      const JobSettings& settings,
                      PeriodicJob& work,
                      const ports::JobLock& lock) const;

private:
    ports::JobLedger& ledger_;
    ports::JobJournal& journal_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::jobs
