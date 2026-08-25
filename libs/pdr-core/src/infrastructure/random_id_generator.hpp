#pragma once

#include "application/ports/id_generator.hpp"
#include "core/types/ids.hpp"

namespace pdr::infrastructure {

/// Настоящий генератор идентификаторов: случайный UUID четвёртой версии.
///
/// Идентификатор — не секрет. Токены доступа, ссылки на приглашение и всё, что
/// нельзя угадывать, берутся не отсюда: это область SEC со своим источником
/// случайности.
class RandomIdGenerator final : public application::ports::IdGenerator {
public:
    RandomIdGenerator() = default;

protected:
    core::IdBytes NextBytes() const override;
};

}  // namespace pdr::infrastructure
