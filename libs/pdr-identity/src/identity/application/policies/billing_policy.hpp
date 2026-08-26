#pragma once

#include <span>

#include "identity/application/policies/policy.hpp"

namespace pdr::identity::policies {

/// Права на деньги: счета, оплата, возвраты, цены.
///
/// Два разных круга, и путать их нельзя. ПЛАТИТ ТОТ, КОМУ ВЫСТАВЛЕНО: ученик за
/// себя, опекун за подопечного, и больше никто — репетитор, оплативший счёт за
/// ученика, это не забота, а деньги, которых никто не переводил. РАСПОРЯЖАЕТСЯ
/// ЦЕНОЙ И ВОЗВРАТОМ тот, кто продаёт: репетитор в своих тарифах и владелец
/// школы в своей школе ([ADR-0008](../../../../../docs/adr/0008-tutor-is-the-seller.md)).
///
/// Счёт видят обе стороны — иначе спор об оплате разбирается пересказом.
class BillingPolicy final : public Policy {
public:
    BillingPolicy() = default;

    static std::span<const Action> Actions() noexcept;

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override;
};

}  // namespace pdr::identity::policies
