#include "identity/core/email.hpp"

#include <algorithm>
#include <cctype>

namespace pdr::identity {
namespace {

constexpr std::size_t kMaxLength = 254;

std::string Lowered(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char symbol : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(symbol))));
    }
    return lowered;
}

bool Shaped(std::string_view text) {
    const auto at = text.find('@');
    if (at == std::string_view::npos || at == 0 || at + 1 >= text.size()) {
        return false;
    }
    if (text.find('@', at + 1) != std::string_view::npos) {
        return false;
    }

    const auto domain = text.substr(at + 1);
    const auto dot = domain.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= domain.size()) {
        return false;
    }

    return text.find(' ') == std::string_view::npos;
}

}  // namespace

core::Result<Email> Email::Parse(std::string_view text) {
    if (text.empty() || text.size() > kMaxLength || !Shaped(text)) {
        return core::Error{
            core::ErrorKind::kValidation, "email_malformed", "адрес почты не похож на адрес почты"};
    }

    return Email{Lowered(text)};
}

}  // namespace pdr::identity
