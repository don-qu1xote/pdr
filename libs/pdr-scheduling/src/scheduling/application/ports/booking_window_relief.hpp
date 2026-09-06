#pragma once

#include <optional>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/booking_window.hpp"

namespace pdr::scheduling::ports {

/// Послабления, выданные конкретному ученику конкретным репетитором.
///
/// Хранится это НА СВЯЗКЕ, а не у ученика и не у репетитора: «ему можно за час»
/// — утверждение про их отношения, а не про кого-то одного. У того же ученика с
/// другим репетитором прав нет никаких, и наоборот.
///
/// Пустой ответ — не отказ, а обычный случай: у большинства пар послаблений нет
/// и никогда не будет.
class BookingWindowRelief {
public:
    BookingWindowRelief(const BookingWindowRelief&) = delete;
    BookingWindowRelief& operator=(const BookingWindowRelief&) = delete;

    virtual ~BookingWindowRelief() = default;

    virtual std::optional<BookingWindows> For(const core::TenantId& tenant,
                                              const core::PersonId& tutor,
                                              const core::PersonId& student) const = 0;

    /// Записать послабление. Что оно действительно ослабляет, а не ужесточает,
    /// решено ДО этого вызова доменным правилом: адаптеру такие вопросы не
    /// задают, он бы ответил на них по-своему.
    virtual core::Result<void> Grant(const BookingRelief& relief) = 0;

protected:
    BookingWindowRelief() = default;
};

}  // namespace pdr::scheduling::ports
