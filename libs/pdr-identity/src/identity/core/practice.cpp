#include "identity/core/practice.hpp"

namespace pdr::identity {

std::string_view Name(Visibility visibility) noexcept {
    switch (visibility) {
        case Visibility::kHidden:
            return "hidden";
        case Visibility::kPending:
            return "pending";
        case Visibility::kPublished:
            return "published";
        case Visibility::kRefused:
            return "refused";
        case Visibility::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<Visibility> ParseVisibility(std::string_view text) {
    for (const auto visibility : kEveryVisibility) {
        if (Name(visibility) == text) {
            return visibility;
        }
    }
    return std::nullopt;
}

std::string_view Name(RefusalReason reason) noexcept {
    switch (reason) {
        case RefusalReason::kNothingToShow:
            return "nothing_to_show";
        case RefusalReason::kLooksBorrowed:
            return "looks_borrowed";
        case RefusalReason::kCallsAway:
            return "calls_away";
        case RefusalReason::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<RefusalReason> ParseRefusalReason(std::string_view text) {
    for (const auto reason : kEveryRefusalReason) {
        if (Name(reason) == text) {
            return reason;
        }
    }
    return std::nullopt;
}

Practice Practice::Opened(core::TenantId tenant, core::Instant at) noexcept {
    return Practice{
        std::move(tenant), Visibility::kHidden, at, std::nullopt, std::nullopt, std::nullopt};
}

Practice Practice::Restore(core::TenantId tenant,
                           Visibility visibility,
                           core::Instant opened_at,
                           std::optional<core::Instant> asked_at,
                           std::optional<core::Instant> decided_at,
                           std::optional<RefusalReason> refusal) noexcept {
    return Practice{std::move(tenant), visibility, opened_at, asked_at, decided_at, refusal};
}

core::Result<Practice> Practice::AskedToPublish(core::Instant at) const {
    if (visibility_ == Visibility::kPending) {
        return core::Error{core::ErrorKind::kConflict,
                           "practice_already_awaits_review",
                           "заявка на публикацию уже в очереди"};
    }
    if (visibility_ == Visibility::kPublished) {
        return core::Error{
            core::ErrorKind::kConflict, "practice_already_published", "практика уже опубликована"};
    }

    return Practice{tenant_, Visibility::kPending, opened_at_, at, std::nullopt, std::nullopt};
}

core::Result<Practice> Practice::Published(core::Instant at) const {
    if (visibility_ != Visibility::kPending) {
        return core::Error{core::ErrorKind::kConflict,
                           "practice_decision_without_request",
                           "решение о публикации принято по практике, которая о ней не просила"};
    }

    return Practice{tenant_, Visibility::kPublished, opened_at_, asked_at_, at, std::nullopt};
}

core::Result<Practice> Practice::RefusedBecause(RefusalReason reason, core::Instant at) const {
    if (visibility_ != Visibility::kPending) {
        return core::Error{core::ErrorKind::kConflict,
                           "practice_decision_without_request",
                           "решение о публикации принято по практике, которая о ней не просила"};
    }

    return Practice{tenant_, Visibility::kRefused, opened_at_, asked_at_, at, reason};
}

Practice Practice::Hidden(core::Instant at) const noexcept {
    return Practice{tenant_, Visibility::kHidden, opened_at_, asked_at_, at, std::nullopt};
}

}  // namespace pdr::identity
