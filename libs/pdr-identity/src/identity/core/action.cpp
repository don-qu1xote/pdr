#include "identity/contract.hpp"

namespace pdr::identity {

std::string_view Name(Action action) noexcept {
    switch (action) {
        case Action::kBookLesson:
            return "book_lesson";
        case Action::kCancelLesson:
            return "cancel_lesson";
        case Action::kRescheduleLesson:
            return "reschedule_lesson";
        case Action::kViewSchedule:
            return "view_schedule";
        case Action::kViewInvoice:
            return "view_invoice";
        case Action::kPayInvoice:
            return "pay_invoice";
        case Action::kIssueRefund:
            return "issue_refund";
        case Action::kSetTariff:
            return "set_tariff";
        case Action::kViewMaterial:
            return "view_material";
        case Action::kEditMaterial:
            return "edit_material";
        case Action::kPublishMaterial:
            return "publish_material";
        case Action::kAssignPlan:
            return "assign_plan";
        case Action::kViewProgress:
            return "view_progress";
        case Action::kRecordAttempt:
            return "record_attempt";
        case Action::kExportProgress:
            return "export_progress";
        case Action::kViewTenantProgress:
            return "view_tenant_progress";
        case Action::kBoundary:
            return "boundary";
    }
    return "boundary";
}

std::optional<Action> ParseAction(std::string_view text) {
    for (const auto action : kEveryAction) {
        if (Name(action) == text) {
            return action;
        }
    }
    return std::nullopt;
}

std::string_view Name(DenyReason reason) noexcept {
    switch (reason) {
        case DenyReason::kAllowed:
            return "allowed";
        case DenyReason::kForeignTenant:
            return "foreign_tenant";
        case DenyReason::kRoleMissing:
            return "role_missing";
        case DenyReason::kNotYours:
            return "not_yours";
        case DenyReason::kNoPolicy:
            return "no_policy";
    }
    return "no_policy";
}

}  // namespace pdr::identity
