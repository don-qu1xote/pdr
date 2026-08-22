#pragma once

#include <cstdint>

#include "application/ports/id_generator.hpp"
#include "core/types/ids.hpp"

namespace pdr::testing {

/// Единственный фейковый генератор идентификаторов проекта.
///
/// Выдаёт подряд идущие значения: 00000000-0000-0000-0000-000000000001,
/// ...0002 и так далее. Такой идентификатор видно в сообщении об ошибке и можно
/// записать в ожидаемый результат, чего не сделаешь со случайным UUID.
class FakeIdGenerator final : public application::ports::IdGenerator {
public:
    explicit FakeIdGenerator(std::uint64_t first = 1) noexcept;

    /// Сколько идентификаторов уже выдано — иногда это и есть проверяемое.
    std::uint64_t Issued() const noexcept;

protected:
    core::IdBytes NextBytes() const override;

private:
    std::uint64_t first_;
    mutable std::uint64_t next_;
};

}  // namespace pdr::testing
