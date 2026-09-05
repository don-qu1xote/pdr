#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/availability.hpp"

namespace pdr::scheduling::ports {

/// Доступность репетитора: прочитать целиком и записать целиком.
///
/// ЦЕЛИКОМ, А НЕ ПО ПРАВИЛУ. Доступность — это одно значение: недельные правила
/// вместе с исключениями. Порт, умеющий добавить правило и убрать правило,
/// заставил бы сценарий собирать её из кусков и держать в голове порядок
/// операций; порт, умеющий отдать и принять целиком, не заставляет.
///
/// Экран доступности и работает так же: репетитор правит расписание и
/// сохраняет, а не отправляет по правилу за раз.
class AvailabilityRepository {
public:
    AvailabilityRepository(const AvailabilityRepository&) = delete;
    AvailabilityRepository& operator=(const AvailabilityRepository&) = delete;

    virtual ~AvailabilityRepository() = default;

    /// Пусто означает, что репетитор доступность не задавал вовсе. Это не то же
    /// самое, что пустая доступность: не задавал — значит занятия просто
    /// требуют подтверждения, а не «он никогда не работает».
    virtual std::optional<Availability> Of(const core::TenantId& tenant,
                                           const core::PersonId& tutor) const = 0;

    virtual core::Result<void> Replace(const core::TenantId& tenant,
                                       const core::PersonId& tutor,
                                       const Availability& availability) = 0;

protected:
    AvailabilityRepository() = default;
};

}  // namespace pdr::scheduling::ports
