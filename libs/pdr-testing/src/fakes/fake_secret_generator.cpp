#include "fakes/fake_secret_generator.hpp"

namespace pdr::testing {

FakeSecretGenerator::FakeSecretGenerator(std::uint64_t first) noexcept
    : first_{first}, next_{first} {}

std::uint64_t FakeSecretGenerator::Issued() const noexcept {
    return next_ - first_;
}

std::string FakeSecretGenerator::NextText(std::size_t bytes) const {
    const auto value = std::to_string(next_++);

    const auto length = (bytes * 4 + 2) / 3;

    std::string text = "fake-secret-";
    text += value;
    text.resize(length > text.size() ? length : text.size(), 'x');
    return text;
}

core::IdBytes FakeSecretGenerator::NextIdBytes() const {
    const std::uint64_t value = next_++;

    core::IdBytes bytes{};
    for (std::size_t index = 0; index < 8; ++index) {
        const auto shift = static_cast<unsigned>((7 - index) * 8);
        bytes[8 + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
    return bytes;
}

}  // namespace pdr::testing
