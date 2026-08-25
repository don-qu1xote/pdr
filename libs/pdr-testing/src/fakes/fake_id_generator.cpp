#include "fakes/fake_id_generator.hpp"

namespace pdr::testing {

FakeIdGenerator::FakeIdGenerator(std::uint64_t first) noexcept : first_{first}, next_{first} {}

std::uint64_t FakeIdGenerator::Issued() const noexcept {
    return next_ - first_;
}

core::IdBytes FakeIdGenerator::NextBytes() const {
    const std::uint64_t value = next_++;

    core::IdBytes bytes{};
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7 - index) * 8);
        bytes[8 + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
    return bytes;
}

}  // namespace pdr::testing
