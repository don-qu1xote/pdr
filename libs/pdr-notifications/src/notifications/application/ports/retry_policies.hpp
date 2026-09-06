#pragma once

#include "core/errors.hpp"
#include "notifications/core/retry.hpp"

namespace pdr::notifications::ports {

/// Откуда берётся политика повторов.
///
/// Порт, а не константа: предел попыток и первая задержка — величины, которые
/// меняют, не пересобирая сервис (`PDR_OUTBOX`). Сценарию при этом всё равно,
/// откуда они пришли, — ему нужна годная политика или отказ.
///
/// Политика ОДНА НА ПЛОЩАДКУ, а не на арендатора: сколько раз стучаться в
/// почтовый узел — свойство узла и наших соединений, а не практики. Репетитору
/// такое настраивать нечего.
class RetryPolicies {
public:
    RetryPolicies(const RetryPolicies&) = delete;
    RetryPolicies& operator=(const RetryPolicies&) = delete;

    virtual ~RetryPolicies() = default;

    virtual core::Result<RetryPolicy> Current() const = 0;

protected:
    RetryPolicies() = default;
};

}  // namespace pdr::notifications::ports
