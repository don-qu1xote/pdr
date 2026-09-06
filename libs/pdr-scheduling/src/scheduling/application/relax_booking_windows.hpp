#pragma once

#include "application/ports/clock.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/booking_window_defaults.hpp"
#include "scheduling/application/ports/booking_window_relief.hpp"
#include "scheduling/core/booking_window.hpp"

namespace pdr::scheduling {

/// Сценарий: дать конкретному ученику более свободные окна.
///
/// УЖЕСТОЧЕНИЕ ОТКЛОНЯЕТСЯ, и отклоняет его домен (`Relax`), а не этот файл.
/// Проверка здесь означала бы, что второй путь записи послабления — из импорта,
/// из административной ручки, из миграции настроек — пройдёт мимо неё.
///
/// Кто выдал послабление, приходит доводом и попадает в строку: «мне разрешили»
/// через полгода превращается в спор, и разрешает его запись, а не память.
class RelaxBookingWindows final {
public:
    struct Request final {
        core::TenantId tenant;
        core::PersonId tutor;
        core::PersonId student;
        core::PersonId granted_by;
        BookingWindows windows;
    };

    RelaxBookingWindows(const ports::BookingWindowDefaults& defaults,
                        ports::BookingWindowRelief& relief,
                        const application::ports::Clock& clock) noexcept;

    core::Result<BookingWindows> Execute(const Request& request) const;

private:
    const ports::BookingWindowDefaults& defaults_;
    ports::BookingWindowRelief& relief_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::scheduling
