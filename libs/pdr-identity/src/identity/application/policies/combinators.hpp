#pragma once

#include <cstdint>
#include <tuple>
#include <utility>

#include "identity/application/policies/policy.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity::policies {

/// Насколько причина отказа полезна тому, кто её услышит.
///
/// Нужна одному месту — `AnyOf`. Когда ни одна ветвь не разрешила, причин
/// столько же, сколько ветвей, и выбрать надо ту, после которой человек знает,
/// что делать: «этот ученик не ваш» полезнее, чем «нет роли», потому что первое
/// говорит, что роль-то есть.
constexpr std::uint8_t Rank(DenyReason reason) noexcept {
    switch (reason) {
        case DenyReason::kAllowed:
            return 0;
        case DenyReason::kRoleMissing:
            return 1;
        case DenyReason::kNotYours:
            return 2;
        case DenyReason::kScopeMissing:
            return 3;
        case DenyReason::kTooYoung:
            return 4;
        case DenyReason::kStudentGrewUp:
            return 5;
        case DenyReason::kForeignTenant:
            return 6;
        case DenyReason::kNoPolicy:
        case DenyReason::kBoundary:
            return 7;
    }
    return 7;
}

/// Есть ли у человека такая роль.
class HasRole final : public Policy {
public:
    explicit HasRole(Role role) noexcept : role_{role} {}

    PolicyDecision Decide(const Subject& subject, Action, const Resource&) const override {
        return subject.Roles().Has(role_) ? Allowed() : Denied(DenyReason::kRoleMissing);
    }

private:
    Role role_;
};

/// Тем ли он приходится ресурсу.
class Tied final : public Policy {
public:
    explicit Tied(Tie tie) noexcept : tie_{tie} {}

    PolicyDecision Decide(const Subject& subject, Action, const Resource&) const override {
        return subject.TiedAs() == tie_ ? Allowed() : Denied(DenyReason::kNotYours);
    }

private:
    Tie tie_;
};

/// Разрешено, когда разрешает КАЖДАЯ часть. Отказ — первый попавшийся, вместе
/// со своей причиной: она и есть та, которая помешала.
template<class... Parts>
class AllOf final : public Policy {
public:
    explicit AllOf(Parts... parts) noexcept : parts_{std::move(parts)...} {}

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override {
        auto decision = Allowed();
        std::apply(
            [&](const auto&... part) {
                ((decision = decision.allowed ? part.Decide(subject, action, resource) : decision),
                 ...);
            },
            parts_);
        return decision;
    }

private:
    std::tuple<Parts...> parts_;
};

/// Разрешено, когда разрешает ХОТЬ ОДНА часть.
///
/// Если не разрешила ни одна — отказ с самой полезной причиной, а не с первой
/// попавшейся: в правиле «репетитор своего, ученик своё, опекун подопечного»
/// у постороннего репетитора причин две, и услышать он должен «это не ваш
/// ученик», а не «нет роли ученика».
template<class... Parts>
class AnyOf final : public Policy {
public:
    explicit AnyOf(Parts... parts) noexcept : parts_{std::move(parts)...} {}

    PolicyDecision Decide(const Subject& subject,
                          Action action,
                          const Resource& resource) const override {
        PolicyDecision best = Denied(DenyReason::kRoleMissing);
        std::apply(
            [&](const auto&... part) {
                (
                    [&] {
                        if (best.allowed) {
                            return;
                        }
                        const auto decision = part.Decide(subject, action, resource);
                        if (decision.allowed || Rank(decision.reason) > Rank(best.reason)) {
                            best = decision;
                        }
                    }(),
                    ...);
            },
            parts_);
        return best;
    }

private:
    std::tuple<Parts...> parts_;
};

}  // namespace pdr::identity::policies
