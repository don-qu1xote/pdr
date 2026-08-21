#pragma once

#include <optional>

#include "core/types/time.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"

namespace pdr::jobs::ports {

/// Журнал прогонов: единственный источник ответа на вопрос «задание ещё живо?».
///
/// Журнал общий на кластер, а не на процесс. Память процесса здесь не подходит
/// дважды: во-первых, воркер, который перезапустился, не помнит ничего и показал
/// бы «не отрабатывало никогда»; во-вторых, показывать возраст последнего
/// прогона обязан КАЖДЫЙ процесс, а работу делает только тот, кто держит
/// блокировку, — остальные узнают о прогоне только отсюда.
class JobJournal {
public:
    JobJournal(const JobJournal&) = delete;
    JobJournal& operator=(const JobJournal&) = delete;

    virtual ~JobJournal() = default;

    virtual void Started(const JobName& job, core::Instant at) = 0;

    virtual void Finished(const JobName& job, const RunRecord& record) = 0;

    /// Последний прогон. Пусто — задание не отрабатывало ни разу.
    virtual std::optional<RunRecord> Last(const JobName& job) const = 0;

protected:
    JobJournal() = default;
};

}  // namespace pdr::jobs::ports
