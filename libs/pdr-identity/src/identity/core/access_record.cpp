#include "identity/core/access_record.hpp"

namespace pdr::identity {

std::string_view Name(ResourceKind kind) noexcept {
    switch (kind) {
        case ResourceKind::kRecording:
            return "recording";
        case ResourceKind::kTranscript:
            return "transcript";
        case ResourceKind::kChat:
            return "chat";
    }
    return "recording";
}

std::optional<ResourceKind> ParseResourceKind(std::string_view text) {
    if (text == "recording") {
        return ResourceKind::kRecording;
    }
    if (text == "transcript") {
        return ResourceKind::kTranscript;
    }
    if (text == "chat") {
        return ResourceKind::kChat;
    }
    return std::nullopt;
}

core::Result<AccessRecord> AccessRecord::Of(core::TenantId tenant,
                                            core::PersonId actor,
                                            core::PersonId subject,
                                            ResourceKind kind,
                                            core::Instant at) {
    if (actor == subject) {
        return core::Error{core::ErrorKind::kValidation,
                           "access_log_self_view",
                           "человек смотрит своё: следа тут не остаётся"};
    }

    return AccessRecord{std::move(tenant), std::move(actor), std::move(subject), kind, at};
}

}  // namespace pdr::identity
