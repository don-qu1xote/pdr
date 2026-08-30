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

std::string_view Name(AccessOutcome outcome) noexcept {
    switch (outcome) {
        case AccessOutcome::kShown:
            return "shown";
        case AccessOutcome::kRefused:
            return "refused";
    }
    return "shown";
}

std::optional<AccessOutcome> ParseAccessOutcome(std::string_view text) {
    if (text == "shown") {
        return AccessOutcome::kShown;
    }
    if (text == "refused") {
        return AccessOutcome::kRefused;
    }
    return std::nullopt;
}

std::optional<ResourceKind> JournalledKind(Action action) noexcept {
    switch (action) {
        case Action::kViewLessonRecording:
            return ResourceKind::kRecording;
        case Action::kViewLessonTranscript:
            return ResourceKind::kTranscript;
        default:
            return std::nullopt;
    }
}

core::Result<AccessRecord> AccessRecord::Of(core::TenantId tenant,
                                            core::PersonId actor,
                                            core::PersonId subject,
                                            ResourceKind kind,
                                            AccessOutcome outcome,
                                            core::Instant at) {
    if (actor == subject) {
        return core::Error{core::ErrorKind::kValidation,
                           "access_log_self_view",
                           "человек смотрит своё: следа тут не остаётся"};
    }

    return AccessRecord{std::move(tenant), std::move(actor), std::move(subject), kind, outcome, at};
}

}  // namespace pdr::identity
