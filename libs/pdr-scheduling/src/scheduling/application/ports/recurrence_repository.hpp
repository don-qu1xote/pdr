#pragma once

#include <optional>
#include <vector>

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

    /// Серии, которые касаются этого человека, — и как репетитора, и как
    /// участника. ОДНИМИ ИДЕНТИФИКАТОРАМИ, а не сериями целиком.
    ///
    /// Идентификаторами потому, что серия — это правило плюс участники плюс
    /// исключения, и собирает её `Find`. Второй сборки, отдающей сразу список,
    /// не заводим: серий у человека единицы, а две сборки одного и того же
    /// расходятся молча.
    ///
    /// И РЕПЕТИТОРА, И УЧАСТНИКА ОДНИМ ВОПРОСОМ. Отпуск репетитора и каникулы
    /// ученика — один механизм с двух сторон, и спрашивающему незачем знать, с
    /// какой стороны стоит человек: у репетитора не найдётся серий, где он
    /// ученик, и наоборот.
    virtual std::vector<core::SeriesId> Of(const core::TenantId& tenant,
                                           const core::PersonId& person) const = 0;

    /// Отметить вхождение отменённым или перенесённым. Второе исключение на ту
    /// же дату — отказ: домен уже сказал, что «отменено и перенесено
    /// одновременно» не значит ничего, и хранилище говорит то же.
    virtual core::Result<void> Record(const core::TenantId& tenant,
                                      const core::SeriesId& id,
                                      const RecurrenceException& exception) = 0;

    /// НОВОЕ ПРАВИЛО У ТОЙ ЖЕ СЕРИИ: та же строка, тот же идентификатор.
    ///
    /// Нужно ровно затем, ради чего и заведён разрез: серия, отодвинутая за
    /// отпуск, кончается днём перед ним, а остаток уезжает новой серией
    /// (`RecurrenceSeries::ShiftPast`). Прежнюю при этом переписывают, а не
    /// заводят заново: занятия, которые она уже провела, ссылаются на неё.
    ///
    /// Участники не трогаются: сдвиг — это про время, а не про состав.
    virtual core::Result<void> Reshape(const RecurrenceSeries& series) = 0;

protected:
    RecurrenceRepository() = default;
};

}  // namespace pdr::scheduling::ports
