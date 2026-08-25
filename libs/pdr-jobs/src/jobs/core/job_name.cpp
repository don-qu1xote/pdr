#include "jobs/core/job_name.hpp"

namespace pdr::jobs {
namespace {

bool IsLowerLetter(char symbol) noexcept {
    return symbol >= 'a' && symbol <= 'z';
}

bool IsDigit(char symbol) noexcept {
    return symbol >= '0' && symbol <= '9';
}

bool IsSeparator(char symbol) noexcept {
    return symbol == '.' || symbol == '-' || symbol == '_';
}

}  // namespace

std::optional<JobName> JobName::Parse(std::string_view name) {
    if (name.empty() || name.size() > kMaxLength) {
        return std::nullopt;
    }
    if (!IsLowerLetter(name.front())) {
        return std::nullopt;
    }
    if (!IsLowerLetter(name.back()) && !IsDigit(name.back())) {
        return std::nullopt;
    }
    for (const char symbol : name) {
        if (!IsLowerLetter(symbol) && !IsDigit(symbol) && !IsSeparator(symbol)) {
            return std::nullopt;
        }
    }
    return JobName{std::string{name}};
}

}  // namespace pdr::jobs
