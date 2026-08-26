#pragma once

#include "application/ports/id_generator.hpp"
#include "identity/application/ports/access_log.hpp"
#include "identity/core/access_record.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Журнал доступа в базе: строка на каждый просмотр чужого,
/// `identity_access_log`.
///
/// Строится ОТ ОБЛАСТИ АРЕНДАТОРА, а не от пула соединений: у журнала есть
/// колонка `tenant_id`, а значит, читать и писать его без объявленного
/// арендатора не должно быть выразимо. Пул в этом заголовке не упоминается
/// вовсе, и это проверяется машиной (`scripts/check_layers.py`).
///
/// Арендатор берётся из записи, а не из области, и сверять их здесь нечем —
/// это и не нужно: политика `identity_access_log_isolation` объявлена с
/// `with check`, поэтому строка чужого арендатора не вставляется вовсе. Отказ
/// даёт база, а не наша внимательность.
///
/// Идентификатор строки выдаёт порт генератора: значений по умолчанию вида
/// `gen_random_uuid()` в схеме нет ни у одной таблицы — иначе в тесте не
/// записать ожидаемый результат.
class PostgresAccessLog final : public ports::AccessLog {
public:
    PostgresAccessLog(infrastructure::db::ScopedTenantContext& scope,
                      const application::ports::IdGenerator& ids) noexcept;

    void Record(const AccessRecord& record) override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
    const application::ports::IdGenerator& ids_;
};

}  // namespace pdr::identity
