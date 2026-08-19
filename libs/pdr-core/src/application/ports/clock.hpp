#pragma once

#include "core/types/time.hpp"

namespace pdr::application::ports {

/// Часы — порт, а не вызов системного времени.
///
/// Календарь у нас центральный: «слот уже начался», «отмена позже чем за сутки»,
/// «счёт просрочен» — всё это сравнения с «сейчас». Сценарий, который берёт
/// время у системы, проверяется только запуском в нужную секунду, поэтому
/// такой тест либо спит, либо мигает. Здесь «сейчас» приходит снаружи, и тест
/// двигает его сам.
///
/// Прямое обращение к системному времени из core и application запрещено и
/// проверяется scripts/check_layers.py.
class Clock {
public:
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;

    virtual ~Clock() = default;

    virtual core::Instant Now() const = 0;

protected:
    Clock() = default;
};

}  // namespace pdr::application::ports
