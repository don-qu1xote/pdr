#pragma once

#include <gtest/gtest.h>

#include "application/ports/clock.hpp"
#include "core/types/time.hpp"

/// @file
/// Contract-набор порта часов: ОДИН набор проверок для фейка и для настоящих
/// часов.
///
/// Фейк часов подставляется в каждый тест расписания и оплат. Если он ведёт себя
/// не как настоящие часы — например, «сейчас» у него убегает назад, — то зелёные
/// тесты отмены за сутки не значат ничего. Поэтому обе реализации отвечают на
/// одни и те же вопросы.
///
/// Требования к «миру»:
///
/// @code
/// struct MyClockWorld {
///     const application::ports::Clock& Clock();
///     static constexpr bool kMovesOnItsOwn = ...;  // идут ли часы сами
/// };
/// @endcode

namespace pdr::testing {

/// Момент, раньше которого не существует ни одного события проекта: у ПДР нет
/// данных за прошлый век, и часы, показывающие 1970 год, сломаны.
inline core::Instant ProjectBeginning() {
    return core::Instant::FromUnixMicros(1704067200000000);
}

template<class World>
class ClockContract : public ::testing::Test {
protected:
    World world_;
};

TYPED_TEST_SUITE_P(ClockContract);

/// «Сейчас» не убегает назад. Часы, идущие назад, дают отрицательную
/// длительность занятия и отмену, случившуюся до записи.
TYPED_TEST_P(ClockContract, TimeNeverGoesBackwards) {
    const auto& clock = this->world_.Clock();

    auto previous = clock.Now();
    for (int reading = 0; reading < 100; ++reading) {
        const auto current = clock.Now();
        ASSERT_FALSE(current < previous) << "часы ушли назад на " << reading << "-м чтении";
        previous = current;
    }
}

/// Часы показывают время этого проекта, а не эпоху Unix.
TYPED_TEST_P(ClockContract, TimeIsPastTheProjectBeginning) {
    const auto& clock = this->world_.Clock();

    EXPECT_FALSE(clock.Now() < ProjectBeginning());
}

/// Часы, которые не идут сами, отвечают одним и тем же: тест, спросивший время
/// дважды, не должен получать разные ответы без своего на то согласия.
TYPED_TEST_P(ClockContract, StoppedClockAnswersTheSame) {
    if (TypeParam::kMovesOnItsOwn) {
        GTEST_SKIP() << "настоящие часы идут сами, это проверяет предыдущий случай";
    }

    const auto& clock = this->world_.Clock();
    EXPECT_TRUE(clock.Now() == clock.Now());
}

REGISTER_TYPED_TEST_SUITE_P(ClockContract,
                            TimeNeverGoesBackwards,
                            TimeIsPastTheProjectBeginning,
                            StoppedClockAnswersTheSame);

}  // namespace pdr::testing

#define PDR_CLOCK_CONTRACT(prefix, world) \
    INSTANTIATE_TYPED_TEST_SUITE_P(prefix, ClockContract, ::testing::Types<world>)
