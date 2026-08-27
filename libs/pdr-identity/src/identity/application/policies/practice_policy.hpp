#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Права на саму практику: кого звать и что с ней делать.
///
/// Действия здесь не про ресурс, а про кабинет целиком, поэтому отношения к
/// ресурсу у них нет: «своё занятие» бывает, «своя практика» — нет, практика у
/// человека одна и он либо её хозяин, либо нет.
///
/// ЗВАТЬ МОЖЕТ И РЕПЕТИТОР, А РАСПОРЯЖАТЬСЯ — ТОЛЬКО ХОЗЯИН. Первое —
/// ежедневная работа: в школе репетитор заводит своих учеников сам, и гонять
/// его за этим к владельцу значит сделать перенос практики чужой заботой.
/// Второе — видимость в подборе, выгрузка и удаление: три действия, у которых
/// цена ошибки не своя, и владелец у них один.
class PracticePolicy final : public Policy {
public:
    PracticePolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
