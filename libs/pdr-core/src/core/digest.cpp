#include "core/digest.hpp"

#include <algorithm>
#include <cstddef>
#include <string>

namespace pdr::core {
namespace {

constexpr std::size_t kLength = 64;

bool IsLowerHex(char symbol) noexcept {
    return (symbol >= '0' && symbol <= '9') || (symbol >= 'a' && symbol <= 'f');
}

}  // namespace

Result<Digest> Digest::Parse(std::string_view text) {
    if (text.size() != kLength) {
        return Error{ErrorKind::kValidation,
                     "digest_malformed",
                     "отпечаток SHA-256 — ровно 64 знака, пришло " + std::to_string(text.size())};
    }
    if (!std::all_of(text.begin(), text.end(), IsLowerHex)) {
        return Error{ErrorKind::kValidation,
                     "digest_malformed",
                     "отпечаток записывается строчными шестнадцатеричными знаками"};
    }

    return Digest{std::string{text}};
}

}  // namespace pdr::core
