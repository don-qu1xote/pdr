#pragma once

#include <userver/concurrent/async_event_source.hpp>
#include <userver/dynamic_config/fwd.hpp>
#include <userver/dynamic_config/snapshot.hpp>
#include <userver/dynamic_config/source.hpp>

#include "scheduling/application/ports/cancellation_policies.hpp"

namespace pdr::scheduling {

/// УМОЛЧАНИЯ ПЛОЩАДКИ из динамического конфига — те же для всех тенантов.
///
/// Тенант политику пока не переопределяет: таблицы настроек у расписания нет, и
/// заводить её вместе с правилами отмены значило бы делать две задачи разом.
/// Порт при этом уже спрашивает про тенанта — появится таблица, поменяется
/// адаптер, а сценарии и домен не заметят.
///
/// ДВЕ ВЕЛИЧИНЫ ИЗ РАЗНЫХ МЕСТ, и это не случайность: окно бесплатной отмены —
/// то же окно, которым меряется бронирование (`PDR_BOOKING_WINDOWS`), а доли
/// удержания — про деньги (`PDR_CANCELLATION_POLICY`). Их правят разные люди по
/// разным поводам, и держать их одной записью значило бы менять разом.
///
/// Связь между величинами схема реестра не выражает: доля вне ноль-ста
/// отвергается доменом (`core::Percent`), и негодная настройка не проходит
/// целиком — прежняя продолжает действовать.
class DynamicConfigCancellationPolicies final : public ports::CancellationPolicies {
public:
    explicit DynamicConfigCancellationPolicies(userver::dynamic_config::Source source);

    ~DynamicConfigCancellationPolicies() override;

    core::Result<CancellationPolicy> Of(const core::TenantId& tenant) const override;

private:
    /// Журнал «было → стало». Политику отмены правят редко и по серьёзному
    /// поводу; запись о смене — единственное, по чему потом восстановят, с
    /// какого дня у людей поменялись условия.
    void OnConfigUpdate(const userver::dynamic_config::Diff& diff);

    userver::dynamic_config::Source source_;
    userver::concurrent::AsyncEventSubscriberScope journal_;
};

}  // namespace pdr::scheduling
