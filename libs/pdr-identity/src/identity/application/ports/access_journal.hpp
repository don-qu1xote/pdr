#pragma once

#include <vector>

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/access_record.hpp"

namespace pdr::identity::ports {

/// Узкий порт: ЧИТАТЬ журнал доступа.
///
/// Отдельно от `ports::AccessLog`, который только пишет, и это решение, а не
/// разделение ради разделения: точка записи не должна уметь читать журнал, а
/// тот, кто показывает журнал ученику, не должен уметь в него дописывать.
/// Порта, которого нет, нельзя случайно позвать.
///
/// Спрашивают всегда «о ком», а не «кто смотрел»: журнал заведён затем, чтобы
/// человек видел заходы к СЕБЕ, и обратный вопрос — «куда заходил этот
/// опекун» — здесь не выражается.
class AccessJournal {
public:
    AccessJournal(const AccessJournal&) = delete;
    AccessJournal& operator=(const AccessJournal&) = delete;

    virtual ~AccessJournal() = default;

    virtual std::vector<AccessRecord> AboutPerson(const core::TenantId& tenant,
                                                  const core::PersonId& subject,
                                                  core::Instant since) const = 0;

protected:
    AccessJournal() = default;
};

}  // namespace pdr::identity::ports
