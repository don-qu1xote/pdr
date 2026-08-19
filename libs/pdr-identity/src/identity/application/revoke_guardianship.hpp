#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "events/bus.hpp"
#include "identity/application/ports/guardianship_repository.hpp"

namespace pdr::identity {

/// Сценарий: отозвать опеку.
///
/// Публикует событие в общий реестр и на этом заканчивает. Кому оно нужно —
/// notifications, reputation, кому угодно ещё — здесь не написано и написано не
/// будет: подписчик добавляется в своём модуле, этот файл не открывают.
class RevokeGuardianship final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId guardian;
        core::PersonId student;
    };

    RevokeGuardianship(ports::GuardianshipRepository& guardianships,
                       const application::ports::Clock& clock,
                       events::Bus& bus) noexcept;

    core::Result<void> Execute(const Request& request) const;

private:
    ports::GuardianshipRepository& guardianships_;
    const application::ports::Clock& clock_;
    events::Bus& bus_;
};

}  // namespace pdr::identity
