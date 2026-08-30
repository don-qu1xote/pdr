#include "identity/application/policies/billing_policy.hpp"

#include <array>

#include "identity/application/policies/capability_policy.hpp"
#include "identity/application/policies/combinators.hpp"
#include "identity/application/policies/guardian_policy.hpp"

namespace pdr::identity::policies {
namespace {

/// Смотреть свой счёт человек вправе в любом возрасте: возраст решает, кто
/// платит, а не кто знает, сколько стоит его учёба.
const AnyOf kPayerSees{AllOf{HasRole{Role::kStudent}, Tied{Tie::kAboutMe}}, GuardianInPayments()};

/// ПЛАТИТ СВОИМИ СРЕДСТВАМИ — со второго порога. До него счёт оплачивает
/// опекун: не потому, что подростку не доверяют, а потому, что своих средств у
/// него нет и распоряжаться он будет чужими.
const AnyOf kMayPay{StudentPayingOwnMoney(), GuardianInPayments()};

/// АВТОПЛАТЁЖ С КАРТЫ ОПЕКУНА — ПРАВО ОПЕКУНА, И УЧЕНИКА ЗДЕСЬ НЕТ НИ В КАКОМ
/// ВОЗРАСТЕ.
///
/// Разведено с оплатой намеренно и явно. «Ученик платит сам» — это его
/// собственные средства; привязанная карта родителя остаётся родительской, и
/// подключить с неё регулярное списание может только тот, чьи это деньги.
/// Сложенные в одно право, они означали бы, что шестнадцатилетний распоряжается
/// чужой картой, — и никто бы этого не заметил до первого списания.
const GuardianRule kCardOwner = GuardianInPayments();

const AnyOf kSeller{AllOf{HasRole{Role::kTutor}, Tied{Tie::kMine}}, HasRole{Role::kOwner}};

const AnyOf kBothSides{kPayerSees, kSeller};

constexpr std::array kActions{
    Action::kViewInvoice,
    Action::kPayInvoice,
    Action::kIssueRefund,
    Action::kSetTariff,
    Action::kManageAutoPayment,
};

}  // namespace

std::span<const Action> BillingPolicy::Actions() noexcept {
    return kActions;
}

PolicyDecision BillingPolicy::Decide(const Subject& subject,
                                     Action action,
                                     const Resource& resource) const {
    switch (action) {
        case Action::kViewInvoice:
            return kBothSides.Decide(subject, action, resource);
        case Action::kPayInvoice:
            return kMayPay.Decide(subject, action, resource);
        case Action::kManageAutoPayment:
            return kCardOwner.Decide(subject, action, resource);
        case Action::kIssueRefund:
        case Action::kSetTariff:
            return kSeller.Decide(subject, action, resource);
        default:
            return Denied(DenyReason::kNoPolicy);
    }
}

}  // namespace pdr::identity::policies
