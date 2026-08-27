#pragma once

#include <cstdint>
#include <string_view>

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/formats/json/value.hpp>

#include "identity/application/ports/maturity_settings.hpp"

namespace pdr::identity {

/// Ключи переменных — по одному объекту на всё дерево. Второй
/// `dynamic_config::Key` с тем же именем даёт вторую ячейку хранилища: подмена
/// значения в тесте тогда не доходит до адаптера, а выглядит это как «конфиг не
/// применился».
extern const userver::dynamic_config::Key<std::int32_t> kSelfAccountAge;
extern const userver::dynamic_config::Key<std::int32_t> kGuardianHandoverDays;

/// Правило совершеннолетия из динамического конфига.
///
/// ДВЕ ВЕЛИЧИНЫ ИЗ РАЗНЫХ МЕСТ, и это не случайность: возрастной порог — вопрос
/// права и страны, а длина окна на решение — вопрос того, как быстро люди
/// читают почту. Их правят разные люди по разным поводам, и держать их одной
/// записью значило бы менять обе разом.
class DynamicConfigMaturitySettings final : public ports::MaturitySettings {
public:
    static constexpr std::string_view kAgeVariable = "PDR_SELF_ACCOUNT_AGE";
    static constexpr std::string_view kHandoverVariable = "PDR_GUARDIAN_HANDOVER_DAYS";

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
