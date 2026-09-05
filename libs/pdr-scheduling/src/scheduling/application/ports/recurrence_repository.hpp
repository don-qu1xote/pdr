#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/recurrence.hpp"

namespace pdr::scheduling::ports {

/// Серия занятий: завести, прочитать, отметить исключение.
///
/// РАЗВЁРНУТОГО СПИСКА ЗДЕСЬ НЕТ НИ В ОДНОМ МЕТОДЕ. Серия хранится правилом
/// (PDR-SCHED-02), и порт это повторяет: «дай занятия серии за месяц» здесь
/// невыразимо, потому что таких строк в базе нет — их считает `Expand`.
class RecurrenceRepository {
public:
    RecurrenceRepository(const RecurrenceRepository&) = delete;
    RecurrenceRepository& operator=(const RecurrenceRepository&) = delete;

    virtual ~RecurrenceRepository() = default;

    /// Завести серию вместе с её участниками. ОДНИМ ОБРАЩЕНИЕМ НА СПИСОК, а не
    /// строкой на участника: групповое занятие на двадцать человек — это
    /// двадцать круговых ходов внутри одной транзакции, и платит за них
    /// репетитор своим ожиданием.
    virtual core::Result<void> Create(const RecurrenceSeries& series) = 0;

    virtual std::optional<RecurrenceSeries> Find(const core::TenantId& tenant,
                                                 const core::SeriesId& id) const = 0;

    /// Отметить вхождение отменённым или перенесённым. Второе исключение на ту
    /// же дату — отказ: домен уже сказал, что «отменено и перенесено
    /// одновременно» не значит ничего, и хранилище говорит то же.
    virtual core::Result<void> Record(const core::TenantId& tenant,
                                      const core::SeriesId& id,
                                      const RecurrenceException& exception) = 0;

protected:
    RecurrenceRepository() = default;
};

}  // namespace pdr::scheduling::ports
