#include "identity/application/policies/matrix.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "identity/application/policies/subject.hpp"
#include "identity/core/guardian_access.hpp"
#include "identity/core/guardian_scope.hpp"

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

/// Субъект для опроса: одна роль, одно отношение, названные уровни опекуна и
/// названные собственные возможности.
Subject Asking(Role role, Tie tie, GuardianScopeSet scopes, Capabilities able) {
    const auto tenant = Somewhere();
    return Subject{tenant,
                   Someone(),
                   RoleSet{}.With(role),
                   tie,
                   GuardianAccess{scopes, GuardianScopeSet{}, GuardianScopeSet{}},
                   able};
}

/// Тот же опрос, когда возраст в вопросе не участвует: возможности открыты все,
/// про них спрашивает отдельная таблица.
Subject Asking(Role role, Tie tie, GuardianScopeSet scopes) {
    return Asking(role, tie, scopes, Capabilities::Everything());
}

/// Что человек может сам в этом возрасте.
Capabilities AbleAt(int years, const AgeThresholds& thresholds) {
    Capabilities able;
    for (const auto capability : kEveryCapability) {
        if (years >= thresholds.Years(ArrivesWith(capability))) {
            able = able.With(capability);
        }
    }
    return able;
}

/// Какие уровни ещё остаются опекуну, когда подопечному столько лет.
///
/// Считается из того же правила, по которому взвешиваются согласия: уровень
/// держится, пока ученик не дорос до порога, с которого решает сам.
GuardianScopeSet OpenToGuardianAt(int years, const AgeThresholds& thresholds) {
    GuardianScopeSet open;
    for (const auto scope : kEveryGuardianScope) {
        if (years < thresholds.Years(WhenStudentDecides(scope))) {
            open = open.With(scope);
        }
    }
    return open;
}

/// Кто может это в таком возрасте: ученик, опекун, оба или никто.
std::string_view WhoAt(const PolicySet& permissions,
                       Action action,
                       int years,
                       const AgeThresholds& thresholds) {
    const Resource resource{Somewhere(), std::nullopt, std::nullopt};

    const bool student =
        permissions
            .Decide(
                Asking(
                    Role::kStudent, Tie::kAboutMe, GuardianScopeSet{}, AbleAt(years, thresholds)),
                action,
                resource)
            .allowed;
    const bool guardian = permissions
                              .Decide(Asking(Role::kGuardian,
                                             Tie::kMyWard,
                                             OpenToGuardianAt(years, thresholds),
                                             Capabilities::Everything()),
                                      action,
                                      resource)
                              .allowed;

    if (student && guardian) {
        return "оба";
    }
    if (student) {
        return "ученик";
    }
    if (guardian) {
        return "опекун";
    }
    return "—";
}

/// Что разрешено этой роли на это действие. Отношения перебираются все подряд:
/// клетка обязана говорить не «да», а «да, но только своё».
///
/// Уровни опекуна и возможности по возрасту открыты все: столбец говорит про
/// роль и отношение, а про уровень и про возраст спрашивают соседние таблицы.
std::string Cell(const PolicySet& permissions, Role role, Action action) {
    const auto tenant = Somewhere();
    const Resource resource{tenant, std::nullopt, std::nullopt};

    std::vector<std::string_view> allowed;
    for (const auto tie : kEveryTie) {
        const auto subject = Asking(role, tie, GuardianScopeSet::Everything());
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

/// Какой уровень согласия нужен опекуну для этого действия.
///
/// Спрашивается у самих политик, а не берётся из таблицы рядом: уровни
/// проверяются по одному, и в столбец попадает тот, с которым опекуну дали.
std::string GuardianLevel(const PolicySet& permissions, Action action) {
    const auto tenant = Somewhere();
    const Resource resource{tenant, std::nullopt, std::nullopt};

    if (permissions
            .Decide(Asking(Role::kGuardian, Tie::kMyWard, GuardianScopeSet::Everything()),
                    action,
                    resource)
            .allowed == false) {
        return "—";
    }

    if (permissions
            .Decide(Asking(Role::kGuardian, Tie::kMyWard, GuardianScopeSet{}), action, resource)
            .allowed) {
        return "не нужен";
    }

    std::string levels;
    for (const auto scope : kEveryGuardianScope) {
        const auto only = GuardianScopeSet{}.With(scope);
        if (!permissions.Decide(Asking(Role::kGuardian, Tie::kMyWard, only), action, resource)
                 .allowed) {
            continue;
        }
        if (!levels.empty()) {
            levels += ", ";
        }
        levels += "`";
        levels += Name(scope);
        levels += "`";
    }
    return levels.empty() ? "—" : levels;
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
        case Action::kViewLessonRecording:
            return "Слушать запись занятия";
        case Action::kViewLessonTranscript:
            return "Читать расшифровку занятия";
        case Action::kViewAccessJournal:
            return "Смотреть, кто заходил в мои данные";
        case Action::kManageGuardianAccess:
            return "Открывать и отзывать доступ опекуну";
        case Action::kWriteReview:
            return "Написать отзыв о репетиторе";
        case Action::kManageAutoPayment:
            return "Подключить списание с карты";
        case Action::kBoundary:
            return "";
    }
    return "";
}

std::string_view Title(AgeThreshold threshold) noexcept {
    switch (threshold) {
        case AgeThreshold::kSlotsAndReviews:
            return "Двигает занятия, пишет отзывы";
        case AgeThreshold::kOwnPayments:
            return "Платит сам, выбирает репетитора";
        case AgeThreshold::kMajority:
            return "Совершеннолетие";
        case AgeThreshold::kBoundary:
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
    out += " Уровень опекуна |\n| --- | --- |";
    for (std::size_t column = 0; column < kEveryRole.size(); ++column) {
        out += " --- |";
    }
    out += " --- |\n";

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
        out += " ";
        out += GuardianLevel(permissions, action);
        out += " |\n";
    }

    return out;
}

std::string RenderAgeMatrix(const PolicySet& permissions, const AgeThresholds& thresholds) {
    std::vector<int> bands{thresholds.Years(kEveryAgeThreshold.front()) - 1};
    for (const auto threshold : kEveryAgeThreshold) {
        bands.push_back(thresholds.Years(threshold));
    }

    std::string out = "| Действие | Код | младше " + std::to_string(bands.front() + 1) + " |";
    for (const auto threshold : kEveryAgeThreshold) {
        out += " с " + std::to_string(thresholds.Years(threshold)) + " |";
    }
    out += "\n| --- | --- |";
    for (std::size_t column = 0; column < bands.size(); ++column) {
        out += " --- |";
    }
    out += "\n";

    for (const auto action : kEveryAction) {
        std::vector<std::string_view> cells;
        bool anyone = false;
        for (const auto years : bands) {
            cells.push_back(WhoAt(permissions, action, years, thresholds));
            anyone = anyone || cells.back() != "—";
        }
        if (!anyone) {
            continue;
        }

        out += "| ";
        out += Title(action);
        out += " | `";
        out += Name(action);
        out += "` |";
        for (const auto& cell : cells) {
            out += " ";
            out += cell;
            out += " |";
        }
        out += "\n";
    }

    return out;
}

namespace {

/// Заменить область между метками собранной таблицей.
core::Result<std::string> Splice(std::string_view document,
                                 std::string_view opening_mark,
                                 std::string_view closing_mark,
                                 const std::string& table) {
    const auto opening = document.find(opening_mark);
    const auto closing = document.find(closing_mark);
    if (opening == std::string_view::npos || closing == std::string_view::npos ||
        closing < opening) {
        return core::Error{core::ErrorKind::kValidation,
                           "permissions_matrix_markers_missing",
                           "в документе нет меток области матрицы"};
    }

    const auto from = opening + opening_mark.size();

    std::string out{document.substr(0, from)};
    out += "\n\n";
    out += table;
    out += "\n";
    out += document.substr(closing);
    return out;
}

}  // namespace

core::Result<std::string> WithMatrix(std::string_view document,
                                     const PolicySet& permissions,
                                     const AgeThresholds& thresholds) {
    auto roles = Splice(document, kMatrixOpening, kMatrixClosing, RenderMatrix(permissions));
    if (!roles) {
        return roles.Failure();
    }

    return Splice(roles.Value(),
                  kAgeMatrixOpening,
                  kAgeMatrixClosing,
                  RenderAgeMatrix(permissions, thresholds));
}

}  // namespace pdr::identity::policies
