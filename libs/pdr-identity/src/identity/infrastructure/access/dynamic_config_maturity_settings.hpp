#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "identity/application/ports/maturity_settings.hpp"

namespace pdr::identity {

/// Возрастные пороги и окно на решение из динамического конфига.
///
/// ЧЕТЫРЕ ВЕЛИЧИНЫ ИЗ РАЗНЫХ МЕСТ, и это не случайность: возрастные пороги —
/// вопрос права и страны, а длина окна на решение — вопрос того, как быстро
/// люди читают почту. Их правят разные люди по разным поводам, и держать их
/// одной записью значило бы менять все разом.
///
/// Связь между порогами схема реестра не выражает: «платит раньше, чем двигает
/// занятия» — правило домена (`AgeThresholds::Compose`), и негодный набор
/// отвергается целиком, а прежний продолжает действовать.
///
/// КЛЮЧЕЙ ЗДЕСЬ НЕТ: все четыре порождены из реестра вместе с умолчаниями и
/// пределами (`dynamic_config/variables/PDR_*.hpp`).
class DynamicConfigMaturitySettings final : public ports::MaturitySettings {
public:
    explicit DynamicConfigMaturitySettings(userver::dynamic_config::Source source);

    ~DynamicConfigMaturitySettings() override;

    core::Result<MaturityRule> Rule() const override;

private:
    /// Журнал «было → стало». Возрастной порог меняют редко и по серьёзному
    /// поводу; запись о смене — единственное, по чему потом восстановят, с
    /// какого дня у семей поменялись права.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::identity
