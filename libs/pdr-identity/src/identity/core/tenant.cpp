#include "identity/core/tenant.hpp"

namespace pdr::identity {
namespace {

std::string Trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\n\r");
    return text.substr(first, last - first + 1);
}

}  // namespace

core::Result<Tenant> Tenant::Compose(core::TenantId id, std::string name) {
    auto trimmed = Trimmed(name);
    if (trimmed.empty()) {
        return core::Error{
            core::ErrorKind::kValidation, "tenant_name_blank", "у арендатора должно быть имя"};
    }

    return Tenant{std::move(id), std::move(trimmed)};
}

}  // namespace pdr::identity
