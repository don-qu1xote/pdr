#pragma once

#include "identity/core/access_record.hpp"

namespace pdr::identity::ports {

/// Узкий порт: положить строку в журнал доступа.
///
/// Чтения здесь нет намеренно. Журнал читают ученик и репетитор — это другой
/// сценарий, другой порт и другие права; порт, которого нет, нельзя случайно
/// позвать из точки записи.
class AccessLog {
public:
    AccessLog(const AccessLog&) = delete;
    AccessLog& operator=(const AccessLog&) = delete;

    virtual ~AccessLog() = default;

    virtual void Record(const AccessRecord& record) = 0;

protected:
    AccessLog() = default;
};

}  // namespace pdr::identity::ports
