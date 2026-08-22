#pragma once

#include <cstdint>

#include "core/types/ids.hpp"

namespace pdr::testing {

/// Идентификатор с номером: `Numbered<core::TenantId>(1)` — это
/// 00000000-0000-0000-0000-000000000001.
///
/// Нужен затем, чтобы в тесте было видно, кто есть кто: «арендатор 1» и
/// «арендатор 2» читаются в сообщении об ошибке, а два случайных UUID — нет.
/// Помогает и там, где идентификаторы должны быть постоянными между прогонами.
template<class Id>
Id Numbered(std::uint64_t number) {
    static_assert(
        core::kIsStrongId<Id>,
        "номер навешивается на типизированный идентификатор: Numbered<core::PersonId>(1)");
    core::IdBytes bytes{};
    for (std::size_t index = 0; index < sizeof(number); ++index) {
        bytes[bytes.size() - 1 - index] =
            static_cast<std::uint8_t>((number >> (8 * index)) & 0xFFU);
    }
    return Id::FromBytes(bytes);
}

}  // namespace pdr::testing
