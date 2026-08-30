#pragma once

#include <array>
#include <cstddef>
#include <span>

#include "identity/application/policies/billing_policy.hpp"
#include "identity/application/policies/content_policy.hpp"
#include "identity/application/policies/guardian_policy.hpp"
#include "identity/application/policies/journal_policy.hpp"
#include "identity/application/policies/media_policy.hpp"
#include "identity/application/policies/policy.hpp"
#include "identity/application/policies/practice_policy.hpp"
#include "identity/application/policies/progress_policy.hpp"
#include "identity/application/policies/review_policy.hpp"
#include "identity/application/policies/scheduling_policy.hpp"
#include "identity/application/ports/configuration_faults.hpp"

namespace pdr::identity::policies {

/// ЕДИНСТВЕННОЕ МЕСТО, где действие связывается с политикой.
///
/// Единственное затем, что «у каждого действия есть политика» проверяется
/// обходом реестра, а обойти можно только то, что лежит в одном месте. Новая
/// область — это новая политика и одна строка здесь, а не правка двадцати
/// хендлеров, каждый из которых понял правило чуть-чуть по-своему.
///
/// ЗНАЧЕНИЕ ПО УМОЛЧАНИЮ — ЗАПРЕТ. Действие без политики не разрешается «пока»
/// и не запрещается молча: оно запрещается и сообщается как поломка настройки
/// (`ports::ConfigurationFaults`). Тихое разрешение здесь стоило бы ровно
/// столько, сколько стоит открытая дверь, о которой никто не знает.
///
/// Границу арендатора набор проверяет САМ, до всякой политики: это не вопрос
/// роли, а вопрос кабинета, и повторять его в каждой политике значит завести
/// четыре места, где его однажды забудут.
class PolicySet final {
public:
    explicit PolicySet(const ports::ConfigurationFaults& faults) noexcept;

    PolicySet(const PolicySet&) = delete;
    PolicySet& operator=(const PolicySet&) = delete;

    PolicyDecision Decide(const Subject& subject, Action action, const Resource& resource) const;

    /// Связана ли политика с этим действием. Спрашивает тест реестра — тот
    /// самый, который роняет сборку раньше, чем действие попадёт в рантайм.
    bool Covers(Action action) const noexcept;

private:
    void Cover(const Policy& policy, std::span<const Action> actions) noexcept;

    SchedulingPolicy scheduling_;
    BillingPolicy billing_;
    ContentPolicy content_;
    ProgressPolicy progress_;
    MediaPolicy media_;
    JournalPolicy journal_;
    ConsentPolicy consents_;
    ReviewPolicy reviews_;
    PracticePolicy practice_;

    std::array<const Policy*, static_cast<std::size_t>(Action::kBoundary)> table_{};
    const ports::ConfigurationFaults& faults_;
};

}  // namespace pdr::identity::policies
