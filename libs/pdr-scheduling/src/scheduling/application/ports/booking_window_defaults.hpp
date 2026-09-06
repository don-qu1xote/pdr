#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/booking_window.hpp"

namespace pdr::scheduling::ports {

/// Откуда берутся окна практики — те, что действуют, пока никто ничего не
/// настраивал.
///
/// ПОРТ, А НЕ КОНСТАНТА И НЕ ЧТЕНИЕ КОНФИГА ИЗ ДОМЕНА. Сегодня за ним
/// динамический конфиг: умолчания площадки, одни на всех. Завтра там же
/// окажется таблица настроек репетитора, и поменяется адаптер, а не сценарии.
///
/// ОБЯЗАТЕЛЬНОЙ НАСТРОЙКИ НЕТ И НЕ БУДЕТ. Порт отвечает всегда: человек попадает
/// в настройки, только когда сам захочет, а до этого работают умолчания. Экран
/// «сначала задайте четыре срока» — это экран, на котором заканчивается половина
/// тех, кто пришёл попробовать.
class BookingWindowDefaults {
public:
    BookingWindowDefaults(const BookingWindowDefaults&) = delete;
    BookingWindowDefaults& operator=(const BookingWindowDefaults&) = delete;

    virtual ~BookingWindowDefaults() = default;

    virtual core::Result<BookingWindows> ForPractice(const core::TenantId& tenant) const = 0;

protected:
    BookingWindowDefaults() = default;
};

}  // namespace pdr::scheduling::ports
