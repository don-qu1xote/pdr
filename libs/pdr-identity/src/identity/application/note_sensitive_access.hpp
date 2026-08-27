#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/access_log.hpp"
#include "identity/core/access_record.hpp"

namespace pdr::identity {

/// Сценарий: отметить, что человек посмотрел чужое.
///
/// ТОЧКА ЗАПИСИ ОДНА. Журнал, в который пишут из десяти мест, через полгода
/// имеет десять представлений о том, что считать доступом; журнал, в который
/// пишут из одного, — одно.
///
/// Момент берётся из порта часов: `now()` базы дал бы вторые часы, а строка,
/// у которой время не то, отвечает на вопрос «кто смотрел в марте» неправильно.
class NoteSensitiveAccess final {
public:
    NoteSensitiveAccess(ports::AccessLog& log, const application::ports::Clock& clock) noexcept;

    core::Result<void> Execute(const core::TenantId& tenant,
                               const core::PersonId& actor,
                               const core::PersonId& subject,
                               ResourceKind kind,
                               AccessOutcome outcome) const;

private:
    ports::AccessLog& log_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
