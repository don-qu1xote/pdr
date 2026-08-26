#include "identity/application/policies/matrix.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "identity/application/policies/subject.hpp"

namespace pdr::identity::policies {
namespace {

constexpr std::array kEveryTie{Tie::kMine, Tie::kAboutMe, Tie::kMyWard, Tie::kNone};

std::string_view Says(Tie tie) noexcept {
    switch (tie) {
        case Tie::kMine:
            return "своё";
        case Tie::kAboutMe:
            return "о себе";
        case Tie::kMyWard:
            return "о подопечном";
        case Tie::kNone:
            return "чужое";
    }
    return "чужое";
}

core::TenantId Somewhere() {
    core::IdBytes bytes{};
    bytes.back() = 1;
    return core::TenantId::FromBytes(bytes);
}

core::PersonId Someone() {
    core::IdBytes bytes{};
    bytes.back() = 2;
    return core::PersonId::FromBytes(bytes);
}

/// Что разрешено этой роли на это действие. Отношения перебираются все подряд:
/// клетка обязана говорить не «да», а «да, но только своё».
std::string Cell(const PolicySet& permissions, Role role, Action action) {
    const auto tenant = Somewhere();
    const Resource resource{tenant, std::nullopt, std::nullopt};

    std::vector<std::string_view> allowed;
    for (const auto tie : kEveryTie) {
        const Subject subject{tenant, Someone(), RoleSet{}.With(role), tie};
        if (permissions.Decide(subject, action, resource).allowed) {
            allowed.push_back(Says(tie));
        }
    }

    if (allowed.empty()) {
        return "—";
    }
    if (allowed.size() == kEveryTie.size()) {
        return "любое в кабинете";
    }

    std::string cell;
    for (const auto& said : allowed) {
        if (!cell.empty()) {
            cell += ", ";
        }
        cell += said;
    }
    return cell;
}

}  // namespace

std::string_view Title(Action action) noexcept {
    switch (action) {
        case Action::kBookLesson:
            return "Записать занятие";
        case Action::kCancelLesson:
            return "Отменить занятие";
        case Action::kRescheduleLesson:
            return "Перенести занятие";
        case Action::kViewSchedule:
            return "Смотреть расписание";
        case Action::kViewInvoice:
            return "Смотреть счёт";
        case Action::kPayInvoice:
            return "Оплатить счёт";
        case Action::kIssueRefund:
            return "Вернуть деньги";
        case Action::kSetTariff:
            return "Назначить цену";
        case Action::kViewMaterial:
            return "Открыть материал";
        case Action::kEditMaterial:
            return "Править материал";
        case Action::kPublishMaterial:
            return "Опубликовать материал";
        case Action::kAssignPlan:
            return "Назначить программу занятий";
        case Action::kViewProgress:
            return "Смотреть, что получается";
        case Action::kRecordAttempt:
            return "Записать попытку";
        case Action::kExportProgress:
            return "Унести историю занятий";
        case Action::kViewTenantProgress:
            return "Смотреть сводку по кабинету";
        case Action::kBoundary:
            return "";
    }
    return "";
}

std::string_view Title(Role role) noexcept {
    switch (role) {
        case Role::kOwner:
            return "Владелец школы";
        case Role::kTutor:
            return "Репетитор";
        case Role::kStudent:
            return "Ученик";
        case Role::kGuardian:
            return "Опекун";
    }
    return "";
}

std::string RenderMatrix(const PolicySet& permissions) {
    std::string out = "| Действие | Код |";
    for (const auto role : kEveryRole) {
        out += " ";
        out += Title(role);
        out += " |";
    }
    out += "\n| --- | --- |";
    for (std::size_t column = 0; column < kEveryRole.size(); ++column) {
        out += " --- |";
    }
    out += "\n";

    for (const auto action : kEveryAction) {
        out += "| ";
        out += Title(action);
        out += " | `";
        out += Name(action);
        out += "` |";
        for (const auto role : kEveryRole) {
            out += " ";
            out += Cell(permissions, role, action);
            out += " |";
        }
        out += "\n";
    }

    return out;
}

}  // namespace pdr::identity::policies

namespace pdr::identity::policies {

core::Result<std::string> WithMatrix(std::string_view document, const PolicySet& permissions) {
    const auto opening = document.find(kMatrixOpening);
    const auto closing = document.find(kMatrixClosing);
    if (opening == std::string_view::npos || closing == std::string_view::npos ||
        closing < opening) {
        return core::Error{core::ErrorKind::kValidation,
                           "permissions_matrix_markers_missing",
                           "в документе нет меток области матрицы прав"};
    }

    const auto from = opening + kMatrixOpening.size();

    std::string out{document.substr(0, from)};
    out += "\n\n";
    out += RenderMatrix(permissions);
    out += "\n";
    out += document.substr(closing);
    return out;
}

}  // namespace pdr::identity::policies
