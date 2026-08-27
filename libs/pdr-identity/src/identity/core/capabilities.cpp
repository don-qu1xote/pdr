#include "identity/core/capabilities.hpp"

namespace pdr::identity {
namespace {

constexpr int kYoungestThinkable = 6;
constexpr int kOldestThinkable = 21;

}  // namespace

std::string_view Name(AgeThreshold threshold) noexcept {
    switch (threshold) {
        case AgeThreshold::kSlotsAndReviews:
            return "slots_and_reviews";
        case AgeThreshold::kOwnPayments:
            return "own_payments";
        case AgeThreshold::kMajority:
            return "majority";
        case AgeThreshold::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<AgeThreshold> ParseAgeThreshold(std::string_view text) {
    for (const auto threshold : kEveryAgeThreshold) {
        if (Name(threshold) == text) {
            return threshold;
        }
    }
    return std::nullopt;
}

std::string_view Name(Capability capability) noexcept {
    switch (capability) {
        case Capability::kMoveOwnSlots:
            return "move_own_slots";
        case Capability::kWriteReview:
            return "write_review";
        case Capability::kDecideOwnGuardianAccess:
            return "decide_own_guardian_access";
        case Capability::kPayOwnMoney:
            return "pay_own_money";
        case Capability::kChooseTutor:
            return "choose_tutor";
        case Capability::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<Capability> ParseCapability(std::string_view text) {
    for (const auto capability : kEveryCapability) {
        if (Name(capability) == text) {
            return capability;
        }
    }
    return std::nullopt;
}

core::Result<AgeThresholds> AgeThresholds::Compose(int slots_and_reviews,
                                                   int own_payments,
                                                   int majority) {
    for (const auto years : {slots_and_reviews, own_payments, majority}) {
        if (years < kYoungestThinkable || years > kOldestThinkable) {
            return core::Error{core::ErrorKind::kValidation,
                               "age_threshold_out_of_range",
                               "возрастной порог самостоятельности вне мыслимых пределов"};
        }
    }

    if (slots_and_reviews > own_payments || own_payments > majority) {
        return core::Error{core::ErrorKind::kValidation,
                           "age_thresholds_out_of_order",
                           "пороги идут не по возрастанию: право приходит раньше предыдущего"};
    }

    return AgeThresholds{slots_and_reviews, own_payments, majority};
}

int AgeThresholds::Years(AgeThreshold threshold) const noexcept {
    switch (threshold) {
        case AgeThreshold::kSlotsAndReviews:
            return slots_and_reviews_;
        case AgeThreshold::kOwnPayments:
            return own_payments_;
        case AgeThreshold::kMajority:
        case AgeThreshold::kBoundary:
            return majority_;
    }
    return majority_;
}

Capabilities Compute(const AgeStatus& age, const AgeThresholds& thresholds) noexcept {
    Capabilities able;
    for (const auto capability : kEveryCapability) {
        if (age.Reached(thresholds.Years(ArrivesWith(capability)))) {
            able = able.With(capability);
        }
    }
    return able;
}

}  // namespace pdr::identity
