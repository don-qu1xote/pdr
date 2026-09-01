#pragma once

#include "application/ports/clock.hpp"
#include "core/types/time.hpp"

namespace pdr::infrastructure {

/// ЧАСЫ ПРОЦЕССА: те, которые умеет двигать контур.
///
/// Внутри поднятого сервиса время спрашивают ЗДЕСЬ, а не у операционной
/// системы. Разница видна ровно один раз — в тесте: `utils::datetime::Now()`
/// подменяется штатно (`utils::datetime::MockNowSet` в C++, фикстура
/// `mocked_time` в контуре), а `std::chrono::system_clock::now()` не
/// подменяется ничем. Тест на протухание ключа, срок сессии или период задания
/// иначе превращается в тест на sleep — а sleep в наборе запрещён
/// (docs/testing.md).
///
/// `SystemClock` при этом остаётся и никуда не денется: он нужен всему, что
/// собирается БЕЗ userver, и проверяется тем же contract-набором. Две
/// реализации одного порта — не дублирование, а разные условия сборки.
class UserverClock final : public application::ports::Clock {
public:
    UserverClock() = default;

    core::Instant Now() const override;
};

}  // namespace pdr::infrastructure
