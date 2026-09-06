#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/application/ports/booking_window_defaults.hpp"
#include "scheduling/application/ports/booking_window_relief.hpp"
#include "scheduling/core/booking_window.hpp"

namespace pdr::scheduling {

/// КАКИЕ ОКНА ДЕЙСТВУЮТ ДЛЯ ЭТОЙ ПАРЫ — один вопрос вместо двух.
///
/// Сценариям нужен ответ, а не два источника и правило их сложения: сценарий,
/// который сам спрашивает умолчания, сам спрашивает послабление и сам их
/// складывает, складывает их по-своему — и второй сценарий сложит иначе.
/// Складывает здесь доменное правило `Relax`, и оно одно на всех.
///
/// Не порт: за ним ничего не стоит, кроме двух других портов. Порт здесь
/// означал бы фейк, повторяющий это же сложение, — то есть проверку сложения
/// его копией.
class WindowsInForce final {
public:
    WindowsInForce(const ports::BookingWindowDefaults& defaults,
                   const ports::BookingWindowRelief& relief) noexcept;

    core::Result<BookingWindows> For(const core::TenantId& tenant,
                                     const core::PersonId& tutor,
                                     const core::PersonId& student) const;

private:
    const ports::BookingWindowDefaults& defaults_;
    const ports::BookingWindowRelief& relief_;
};

}  // namespace pdr::scheduling
