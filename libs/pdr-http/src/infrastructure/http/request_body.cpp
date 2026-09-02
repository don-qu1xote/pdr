#include "infrastructure/http/request_body.hpp"

#include <cstddef>

namespace pdr::infrastructure::http {
namespace {

constexpr std::string_view kPathOpens = "Error at path '";
constexpr char kPathCloses = '\'';

}  // namespace

std::string FieldOfMessage(std::string_view message) {
    const auto opened = message.find(kPathOpens);
    if (opened == std::string_view::npos) {
        return std::string{};
    }

    const auto begins = opened + kPathOpens.size();
    const auto closed = message.find(kPathCloses, begins);
    if (closed == std::string_view::npos) {
        return std::string{};
    }

    return std::string{message.substr(begins, closed - begins)};
}

}  // namespace pdr::infrastructure::http
