#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/availability_repository.hpp"
#include "scheduling/core/availability.hpp"

namespace pdr::scheduling {

/// Сценарий: записать доступность репетитора целиком.
///
/// ЦЕЛИКОМ, А НЕ ПО ПРАВИЛУ — так же, как устроен порт и как устроен экран:
/// репетитор правит расписание и сохраняет. Сценарий, принимающий правило за
/// раз, заставил бы клиента помнить порядок операций и оставлял бы половину
/// расписания записанной, когда связь оборвалась посередине.
///
/// Собранная доменом доступность приходит сюда готовой: собрать её нельзя
/// иначе как через `Availability::Compose`, а он и есть место, где живут
/// правила.
class SetAvailability final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId tutor;
        Availability availability;
    };

    explicit SetAvailability(ports::AvailabilityRepository& availability) noexcept;

    core::Result<void> Execute(const Request& request) const;

private:
    ports::AvailabilityRepository& availability_;
};

}  // namespace pdr::scheduling
