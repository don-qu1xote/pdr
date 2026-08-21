#pragma once

#include <optional>

#include <userver/storages/postgres/cluster.hpp>

#include "core/types/time.hpp"
#include "jobs/application/ports/job_journal.hpp"
#include "jobs/core/job_name.hpp"
#include "jobs/core/run_record.hpp"

namespace pdr::jobs {

/// Журнал прогонов в базе: одна строка на задание, `jobs_run`.
///
/// Арендатора у таблицы нет намеренно: одиночное задание принадлежит кластеру, а
/// не арендатору, — поэтому здесь обычный кластер, а не сессия под арендатором.
/// Это единственное место в контексте, где запрос идёт мимо RLS, и мимо она идёт
/// потому, что защищать в журнале нечего: имя задания, время и счётчики.
///
/// Моменты приходят из порта часов и уезжают в базу параметрами: `now()` базы
/// дал бы вторые часы, а прогон, у которого начало по одним часам, а конец по
/// другим, показывает длительность, которой не было.
class PostgresJobJournal final : public ports::JobJournal {
public:
    explicit PostgresJobJournal(userver::storages::postgres::ClusterPtr cluster);

    void Started(const JobName& job, core::Instant at) override;

    void Finished(const JobName& job, const RunRecord& record) override;

    std::optional<RunRecord> Last(const JobName& job) const override;

private:
    userver::storages::postgres::ClusterPtr cluster_;
};

}  // namespace pdr::jobs
