#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/availability_repository.hpp"
#include "scheduling/core/availability.hpp"

namespace pdr::scheduling {

/// Сценарий: показать доступность репетитора.
///
/// «НЕ ЗАДАВАЛ» И «ЗАДАЛ ПУСТУЮ» — РАЗНЫЕ ИСХОДЫ, и сливать их нельзя. Не
/// задавал — отказ `availability_not_set`: занятия к нему просто требуют
/// подтверждения. Задал и стёр всё — доступность без правил: он не работает
/// никогда. Пустой ответ на оба вопроса лишил бы репетитора возможности
/// сказать «не работаю».
class GetAvailability final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId tutor;
    };

    explicit GetAvailability(const ports::AvailabilityRepository& availability) noexcept;

    core::Result<Availability> Execute(const Request& request) const;

private:
    const ports::AvailabilityRepository& availability_;
};

}  // namespace pdr::scheduling
