#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Кто пишет отзыв о репетиторе.
///
/// ТОЛЬКО САМ УЧЕНИК И ТОЛЬКО С ПЕРВОГО ПОРОГА. Опекуна здесь нет ни при каком
/// уровне доступа, и это решение, а не пропуск: отзыв — рассказ о том, каково
/// быть учеником этого человека, и написать его за другого нельзя. До порога
/// отзыва просто не существует.
///
/// Опекун узнаёт, ЧТО отзыв написан, и не узнаёт, что в нём: право высказаться
/// без надзора и есть содержание порога. За это отвечает
/// `identity::NotifyGuardianOfAct`, а не эта политика — политика решает, кому
/// можно, а не кому сообщить.
class ReviewPolicy final : public Policy {
public:
    ReviewPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
