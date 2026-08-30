#include "identity/core/personal_data.hpp"

namespace pdr::identity {
namespace {

template<class Value, std::size_t Size>
std::optional<Value> ParseByName(std::string_view text, const std::array<Value, Size>& every) {
    for (const auto value : every) {
        if (Name(value) == text) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace

std::string_view Name(PersonalDataCategory category) noexcept {
    switch (category) {
        case PersonalDataCategory::kAccount:
            return "account";
        case PersonalDataCategory::kChildAndGuardian:
            return "child_and_guardian";
        case PersonalDataCategory::kScheduleAndAttendance:
            return "schedule_and_attendance";
        case PersonalDataCategory::kPaymentsAndReceipts:
            return "payments_and_receipts";
        case PersonalDataCategory::kMessages:
            return "messages";
        case PersonalDataCategory::kRecordingsAndTranscripts:
            return "recordings_and_transcripts";
        case PersonalDataCategory::kLearningResults:
            return "learning_results";
        case PersonalDataCategory::kTechnicalRecords:
            return "technical_records";
        case PersonalDataCategory::kBoundary:
            break;
    }
    return "account";
}

std::optional<PersonalDataCategory> ParsePersonalDataCategory(std::string_view text) {
    return ParseByName(text, kEveryPersonalDataCategory);
}

std::string_view Name(Recipient recipient) noexcept {
    switch (recipient) {
        case Recipient::kPaymentProvider:
            return "payment_provider";
        case Recipient::kReceiptService:
            return "receipt_service";
        case Recipient::kModelProvider:
            return "model_provider";
        case Recipient::kHandwriting:
            return "handwriting";
        case Recipient::kHosting:
            return "hosting";
        case Recipient::kBoundary:
            break;
    }
    return "hosting";
}

std::optional<Recipient> ParseRecipient(std::string_view text) {
    return ParseByName(text, kEveryRecipient);
}

}  // namespace pdr::identity
