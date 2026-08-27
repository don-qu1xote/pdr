#pragma once

#include <span>

#include "identity/application/policies/combinators.hpp"
#include "identity/core/guardian_scope.hpp"

namespace pdr::identity::policies {

/// Открыт ли опекуну этот уровень.
///
/// Три ответа, а не два, и третий — не придирка. «Вам не открывали записи» и
/// «ученик вырос, слово за ним» отправляют человека к разным людям: в первом
/// случае к репетитору, во втором — к самому ученику, единственному, кто может
/// открыть доступ обратно.
class HasScope final : public Policy {
public:
    explicit HasScope(GuardianScope scope) noexcept : scope_{scope} {}

    PolicyDecision Decide(const Subject& subject, Action, const Resource&) const override {
        if (subject.AsGuardian().Open().Has(scope_)) {
            return Allowed();
        }
        if (subject.AsGuardian().AwaitsStudent().Has(scope_)) {
            return Denied(DenyReason::kStudentGrewUp);
        }
        return Denied(DenyReason::kScopeMissing);
    }

private:
    GuardianScope scope_;
};

/// Правило опекуна: он опекун, это его подопечный, и уровень ему открыт.
///
/// Тип назван, а не выведен на месте, ровно затем, чтобы «опекун без проверки
/// уровня» нельзя было собрать случайно: три части, и ни одну не выкинуть.
using GuardianRule = AllOf<HasRole, Tied, HasScope>;

/// Правила по уровням. Каждое называет свой уровень ЯВНО: доступ к записям
/// занятий не должен получаться из доступа к расписанию оттого, что кто-то
/// переиспользовал соседнюю строку.
GuardianRule GuardianInSchedule() noexcept;
GuardianRule GuardianInPayments() noexcept;
GuardianRule GuardianInNotes() noexcept;
GuardianRule GuardianInRecordings() noexcept;

/// Кто открывает и отзывает уровни доступа опекуну.
///
/// ОПЕКУНА ЗДЕСЬ НЕТ, и это главное решение файла. Опекун, который сам себе
/// открывает записи занятий, — не согласие, а формальность; согласие даёт кто-то
/// другой. Репетитор заводит родительский доступ вместе с учеником, а
/// повзрослевший ученик распоряжается им сам — той же ручкой, тем же действием.
///
/// Владельца школы тоже нет: он ведёт школу, а не решает, кто слушает записи
/// чужого ребёнка.
class ConsentPolicy final : public Policy {
public:
    ConsentPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
