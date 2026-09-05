#pragma once

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "scheduling/core/cancellation_policy.hpp"

namespace pdr::scheduling::ports {

/// Откуда берётся политика отмены этого тенанта.
///
/// ПОРТ, А НЕ КОНСТАНТА И НЕ ЧТЕНИЕ КОНФИГА ИЗ ДОМЕНА. Сегодня за ним стоит
/// динамический конфиг — платформенные умолчания, одни на всех
/// (`PDR_CANCELLATION_POLICY`). Завтра там же окажется таблица настроек
/// репетитора, и поменяется адаптер, а не сценарии.
///
/// Отказ возвращается значением: негодная настройка — это ответ «так нельзя», а
/// не авария, и старое значение при ней продолжает действовать.
class CancellationPolicies {
public:
    CancellationPolicies(const CancellationPolicies&) = delete;
    CancellationPolicies& operator=(const CancellationPolicies&) = delete;

    virtual ~CancellationPolicies() = default;

    virtual core::Result<CancellationPolicy> Of(const core::TenantId& tenant) const = 0;

protected:
    CancellationPolicies() = default;
};

}  // namespace pdr::scheduling::ports
