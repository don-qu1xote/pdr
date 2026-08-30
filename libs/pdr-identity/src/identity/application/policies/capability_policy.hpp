#pragma once

#include "identity/application/policies/combinators.hpp"
#include "identity/core/capabilities.hpp"

namespace pdr::identity::policies {

/// Дорос ли человек до этого сам.
///
/// ПРАВО ПРИХОДИТ САМО, А НЕ ПО ЗАЯВКЕ. Здесь не спрашивают, выдал ли кто-то
/// подростку самостоятельность: набор возможностей посчитан из даты рождения и
/// порогов конфига (`identity::Compute`), и в день рождения он меняется без
/// чьего-либо участия.
///
/// Отказ называется своим словом. «Ещё не дорос» — единственный отказ в
/// системе, который проходит сам собой, и человеку надо сказать именно это, а
/// не «обратитесь к репетитору»: обращаться не к кому.
class Able final : public Policy {
public:
    explicit Able(Capability capability) noexcept : capability_{capability} {}

    PolicyDecision Decide(const Subject& subject, Action, const Resource&) const override {
        return subject.Able().Has(capability_) ? Allowed() : Denied(DenyReason::kTooYoung);
    }

private:
    Capability capability_;
};

/// Правило ученика: он ученик, это про него, и он дорос.
///
/// Тип назван, а не выведен на месте, ровно затем, чтобы «ученик без проверки
/// возраста» нельзя было собрать случайно. Там, где возраст не при чём —
/// смотреть своё расписание, свой счёт, свой журнал доступа, — правило и не
/// используется: видеть своё человек вправе в любом возрасте.
using StudentRule = AllOf<HasRole, Tied, Able>;

/// Двигать свои занятия: перенос и отмена. Первый порог.
StudentRule StudentMovingOwnSlots() noexcept;

/// Писать отзыв о репетиторе. Тот же первый порог.
StudentRule StudentWritingReview() noexcept;

/// Распоряжаться доступом собственного опекуна. Тот же первый порог: с него же
/// уровни начинают переходить к ученику, и распорядиться ими должен уметь он.
StudentRule StudentDecidingGuardianAccess() noexcept;

/// Платить своими средствами. Второй порог.
StudentRule StudentPayingOwnMoney() noexcept;

/// Выбирать репетитора и записываться к нему. Тот же второй порог.
StudentRule StudentChoosingTutor() noexcept;

}  // namespace pdr::identity::policies
