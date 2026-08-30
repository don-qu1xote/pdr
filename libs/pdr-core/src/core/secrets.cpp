#include "core/secrets.hpp"

namespace pdr::core {

std::string_view Name(SecretPurpose purpose) noexcept {
    switch (purpose) {
        case SecretPurpose::kDatabase:
            return "database";
        case SecretPurpose::kPaymentProvider:
            return "payment_provider";
        case SecretPurpose::kWebhookSigning:
            return "webhook_signing";
        case SecretPurpose::kVideoProvider:
            return "video_provider";
        case SecretPurpose::kBoundary:
            break;
    }
    return "database";
}

std::optional<SecretPurpose> ParseSecretPurpose(std::string_view text) {
    for (std::size_t index = 0; index < static_cast<std::size_t>(SecretPurpose::kBoundary);
         ++index) {
        const auto purpose = static_cast<SecretPurpose>(index);
        if (Name(purpose) == text) {
            return purpose;
        }
    }
    return std::nullopt;
}

}  // namespace pdr::core
