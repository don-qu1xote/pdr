#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/guardian_consents.hpp"
#include "identity/core/guardian_consent.hpp"

namespace pdr::identity {

struct RevokeGuardianScopeRequest final {
    core::TenantId tenant;
    core::PersonId guardian;
    core::PersonId student;
    GuardianScope scope;
    core::PersonId revoked_by;
};

/// Закрыть опекуну один уровень доступа.
///
/// ОТЗЫВ — ЭТО ДАТА, А НЕ УДАЛЕНИЕ СТРОКИ. По этому доступу человек смотрел
/// чужие данные; на вопрос «кто имел доступ в марте» отвечает журнал, а
/// удалённая строка отвечает «никто», и это неправда.
///
/// Повторный отзыв — ожидаемый отказ, а не авария: кнопку нажимают дважды.
class RevokeGuardianScope final {
public:
    RevokeGuardianScope(ports::GuardianConsents& consents,
                        const application::ports::Clock& clock) noexcept;

    core::Result<void> Execute(const RevokeGuardianScopeRequest& request) const;

private:
    ports::GuardianConsents& consents_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
