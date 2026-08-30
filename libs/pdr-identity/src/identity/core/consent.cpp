#include "identity/core/consent.hpp"

#include <utility>

namespace pdr::identity {
namespace {

constexpr int kFirstVersion = 1;

}  // namespace

std::string_view Name(ConsentKind kind) noexcept {
    switch (kind) {
        case ConsentKind::kProcessing:
            return "processing";
        case ConsentKind::kRecordings:
            return "recordings";
        case ConsentKind::kBoundary:
            break;
    }
    return "processing";
}

std::optional<ConsentKind> ParseConsentKind(std::string_view text) {
    for (const auto kind : kEveryConsentKind) {
        if (Name(kind) == text) {
            return kind;
        }
    }
    return std::nullopt;
}

std::string_view Name(ConsentAction action) noexcept {
    switch (action) {
        case ConsentAction::kSignUpCheckbox:
            return "sign_up_checkbox";
        case ConsentAction::kSettingsCheckbox:
            return "settings_checkbox";
        case ConsentAction::kVersionAccepted:
            return "version_accepted";
        case ConsentAction::kBoundary:
            break;
    }
    return "sign_up_checkbox";
}

std::optional<ConsentAction> ParseConsentAction(std::string_view text) {
    for (const auto action : kEveryConsentAction) {
        if (Name(action) == text) {
            return action;
        }
    }
    return std::nullopt;
}

std::string_view Name(VersionChange change) noexcept {
    switch (change) {
        case VersionChange::kSubstantial:
            return "substantial";
        case VersionChange::kCosmetic:
            return "cosmetic";
        case VersionChange::kBoundary:
            break;
    }
    return "substantial";
}

std::optional<VersionChange> ParseVersionChange(std::string_view text) {
    if (text == Name(VersionChange::kSubstantial)) {
        return VersionChange::kSubstantial;
    }
    if (text == Name(VersionChange::kCosmetic)) {
        return VersionChange::kCosmetic;
    }
    return std::nullopt;
}

core::Result<PolicyVersion> PolicyVersion::Of(int number) {
    if (number < kFirstVersion) {
        return core::Error{core::ErrorKind::kValidation,
                           "policy_version_out_of_range",
                           "версии перечня нумеруются с единицы"};
    }
    return PolicyVersion{number};
}

ConsentRecord::ConsentRecord(ConsentRecordId id,
                             core::TenantId tenant,
                             core::PersonId subject,
                             core::PersonId given_by,
                             ConsentKind kind,
                             PolicyVersion version,
                             ConsentAction action,
                             core::Instant given_at,
                             std::optional<core::Instant> withdrawn_at) noexcept
    : id_{std::move(id)},
      tenant_{std::move(tenant)},
      subject_{std::move(subject)},
      given_by_{std::move(given_by)},
      kind_{kind},
      version_{version},
      action_{action},
      given_at_{given_at},
      withdrawn_at_{withdrawn_at} {}

core::Result<ConsentRecord> ConsentRecord::Give(ConsentRecordId id,
                                                core::TenantId tenant,
                                                core::PersonId subject,
                                                core::PersonId given_by,
                                                ConsentKind kind,
                                                PolicyVersion version,
                                                ConsentAction action,
                                                core::Instant given_at) {
    if (kind == ConsentKind::kBoundary) {
        return core::Error{
            core::ErrorKind::kValidation, "consent_kind_unknown", "такого вида согласия нет"};
    }
    if (action == ConsentAction::kBoundary) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_action_unknown",
                           "согласие без действия ничем не подтверждается"};
    }

    return ConsentRecord{std::move(id),
                         std::move(tenant),
                         std::move(subject),
                         std::move(given_by),
                         kind,
                         version,
                         action,
                         given_at,
                         std::nullopt};
}

ConsentRecord ConsentRecord::Restore(ConsentRecordId id,
                                     core::TenantId tenant,
                                     core::PersonId subject,
                                     core::PersonId given_by,
                                     ConsentKind kind,
                                     PolicyVersion version,
                                     ConsentAction action,
                                     core::Instant given_at,
                                     std::optional<core::Instant> withdrawn_at) {
    return ConsentRecord{std::move(id),
                         std::move(tenant),
                         std::move(subject),
                         std::move(given_by),
                         kind,
                         version,
                         action,
                         given_at,
                         withdrawn_at};
}

core::Result<ConsentRecord> ConsentRecord::Withdrawn(core::Instant at) const {
    if (withdrawn_at_.has_value()) {
        return core::Error{core::ErrorKind::kConflict,
                           "consent_already_withdrawn",
                           "это согласие отозвали раньше"};
    }
    if (at < given_at_) {
        return core::Error{core::ErrorKind::kValidation,
                           "consent_withdrawn_before_given",
                           "отзыв стоит раньше согласия"};
    }

    ConsentRecord withdrawn{*this};
    withdrawn.withdrawn_at_ = at;
    return withdrawn;
}

}  // namespace pdr::identity
