#include "infrastructure/random_id_generator.hpp"

#include <cstdint>
#include <random>

namespace pdr::infrastructure {
namespace {

/// Свой поток случайности на поток исполнения: общий на всех пришлось бы
/// защищать замком на каждом идентификаторе.
std::mt19937_64& Engine() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    return engine;
}

}  // namespace

core::IdBytes RandomIdGenerator::NextBytes() const {
    core::IdBytes bytes{};

    std::uniform_int_distribution<std::uint32_t> byte{0, 255};
    for (auto& value : bytes) {
        value = static_cast<std::uint8_t>(byte(Engine()));
    }

    // Версия 4 и вариант по RFC 4122: чтобы чужой разбор узнавал наш UUID.
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0FU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3FU) | 0x80U);

    return bytes;
}

}  // namespace pdr::infrastructure
