#pragma once

#include <optional>

#include "core/types/time.hpp"
#include "infrastructure/db/unscoped_access.hpp"
#include "jobs/application/ports/job_journal.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"

namespace pdr::jobs {

/// Журнал прогонов в базе: одна строка на задание, `jobs_run`.
///
/// Арендатора у таблицы нет намеренно: одиночное задание принадлежит кластеру, а
/// не арендатору. Поэтому запрос идёт через `db::UnscopedAccess` — вторую и
/// последнюю дверь к соединениям, названную так, чтобы её было видно на ревью.
/// Причина названа перечислением, а не словами: `kClusterWideJobJournal`.
///
/// Защищать в журнале нечего: имя задания, время и счётчики. Ни одной колонки
/// с арендатором в `jobs_run` нет, и политике не на что было бы смотреть.
///
/// Моменты приходят из порта часов и уезжают в базу параметрами: `now()` базы
/// дал бы вторые часы, а прогон, у которого начало по одним часам, а конец по
/// другим, показывает длительность, которой не было.
class PostgresJobJournal final : public ports::JobJournal {
public:
    explicit PostgresJobJournal(const infrastructure::db::UnscopedAccess& access) noexcept;

    void Started(const JobName& job, core::Instant at) override;

    void Finished(const JobName& job, const RunRecord& record) override;

    std::optional<RunRecord> Last(const JobName& job) const override;

private:
    const infrastructure::db::UnscopedAccess& access_;
};

}  // namespace pdr::jobs
