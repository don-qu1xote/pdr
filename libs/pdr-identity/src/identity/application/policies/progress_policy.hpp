#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Права на то, что получается у ученика.
///
/// ПОПЫТКУ ЗАПИСЫВАЕТ ТОЛЬКО САМ УЧЕНИК. Ни репетитор, ни опекун: попытка — это
/// свидетельство того, что человек решал, и подставленная кем-то другим она
/// портит не строку в таблице, а всю модель, которая на этих строках стоит.
///
/// ИСТОРИЮ УНОСИТ ТОТ, ЧЬЯ ОНА: ученик или его опекун. Репетитора здесь нет —
/// он видит прогресс своего ученика, но выгрузка это «данные ваши», а данные
/// эти не его ([openness.md](../../../../../docs/architecture/openness.md)).
///
/// Владелец школы видит СВОДКУ по кабинету и не видит прогресса конкретного
/// ученика: чтобы вести школу, нужны первые числа, а не вторые.
class ProgressPolicy final : public Policy {
public:
    ProgressPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
