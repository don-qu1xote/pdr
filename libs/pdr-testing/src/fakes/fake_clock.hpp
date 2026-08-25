#pragma once

#include "application/ports/clock.hpp"
#include "core/types/time.hpp"

namespace pdr::testing {

/// Единственные фейковые часы проекта. Свои часы в каждом тестовом файле — это
/// пять разных представлений о том, что такое «сейчас», и пять мест, куда
/// придётся вносить правку, когда порт поменяется.
///
/// Время двигается вызовом Advance и мгновенно: тест на «отмену не позже чем за
/// сутки» проходит за микросекунды и не зависит от того, когда его запустили.
/// Никаких sleep.
///
/// Часы рассчитаны на однопоточный unit-тест; для проверок с сопрограммами
/// нужен свой примитив, и он появится вместе с ними.
class FakeClock final : public application::ports::Clock {
public:
    /// Произвольная, но постоянная точка отсчёта: 2024-01-01T00:00:00Z.
    /// Постоянная — чтобы ожидаемые значения в тестах можно было записать руками.
    static core::Instant DefaultStart() noexcept;

    FakeClock() noexcept;
    explicit FakeClock(core::Instant start) noexcept;

    core::Instant Now() const override;

    void Advance(core::Instant::Duration delta) noexcept;
    void SetNow(core::Instant instant) noexcept;

private:
    core::Instant now_;
};

}  // namespace pdr::testing
