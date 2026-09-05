#pragma once

#include <string>

#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/local_time.hpp"
#include "core/types/time.hpp"
#include "scheduling/application/ports/recurrence_repository.hpp"
#include "scheduling/core/lesson.hpp"
#include "scheduling/core/recurrence.hpp"

namespace pdr::scheduling {

/// Сценарий: завести регулярные занятия.
///
/// РАЗВЁРТКИ ЗДЕСЬ НЕТ. Серия заводится правилом, и занятий в базе после этого
/// сценария не появляется ни одного: сорок строк, созданных при заведении,
/// расходятся с правилом на первом же переносе (PDR-SCHED-02). Занятия серии
/// считаются по запросу — `Expand`, — и считать их некому до тех пор, пока в
/// дереве нет порта правил зоны.
///
/// Правило приходит строкой RRULE и разбирается доменом. Разбор здесь не
/// повторяется и не смягчается: часть вне поддержанного подмножества —
/// названный отказ, а не тихий пропуск.
class CreateSeries final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId tutor;
        core::PersonId student;

        /// Правило повторения строкой RFC 5545 — тем же подмножеством, которое
        /// разбирает `RecurrenceRule::Parse`.
        std::string rrule;

        core::Date starts_on;
        core::LocalTime at;
        core::TimeZone zone;
        Lesson::Duration duration;
    };

    CreateSeries(ports::RecurrenceRepository& series,
                 const application::ports::IdGenerator& ids) noexcept;

    /// СЕРИЯ ЦЕЛИКОМ, а не её идентификатор: правило, прочитанное разбором,
    /// может отличаться от присланного порядком частей, и вызывающему нужно то,
    /// которое записано, а не то, которое он прислал.
    core::Result<RecurrenceSeries> Execute(const Request& request) const;

private:
    ports::RecurrenceRepository& series_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::scheduling
