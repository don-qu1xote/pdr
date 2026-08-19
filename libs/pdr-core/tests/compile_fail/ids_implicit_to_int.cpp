// Этот файл ОБЯЗАН не собираться: идентификатор — не число, и в арифметику,
// счётчики и индексы он не превращается.
#include <cstdint>

#include "core/types/ids.hpp"

int main() {
    const auto lesson = pdr::core::LessonId::FromBytes(pdr::core::IdBytes{});

    const std::int64_t value = lesson;
    return static_cast<int>(value);
}
