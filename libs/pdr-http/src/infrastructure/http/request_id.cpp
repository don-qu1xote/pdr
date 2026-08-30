#include "infrastructure/http/request_id.hpp"

#include <algorithm>

#include <userver/tracing/span.hpp>
#include <userver/utils/rand.hpp>
#include <userver/utils/uuid4.hpp>

namespace pdr::infrastructure::http {
namespace {

bool Usable(char symbol) noexcept {
    return (symbol >= 'a' && symbol <= 'z') || (symbol >= 'A' && symbol <= 'Z') ||
           (symbol >= '0' && symbol <= '9') || symbol == '-' || symbol == '_' || symbol == '.' ||
           symbol == ':';
}

}  // namespace

bool IsUsableRequestId(std::string_view value) noexcept {
    if (value.empty() || value.size() > kRequestIdLimit) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), Usable);
}

std::string TracingRequestId() {
    const auto* span = userver::tracing::Span::CurrentSpanUnchecked();
    if (span == nullptr) {
        return userver::utils::generators::GenerateUuid();
    }
    return std::string{span->GetLink()};
}

}  // namespace pdr::infrastructure::http
