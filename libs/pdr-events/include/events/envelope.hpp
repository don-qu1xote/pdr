#pragma once

#include "core/types/ids.hpp"
#include "core/types/time.hpp"

namespace pdr::events {

/// Общие поля любого доменного события.
///
/// Арендатор — потому что событие всегда происходит в его границах, и
/// подписчик обязан это видеть, а не догадываться. Момент — по часам издателя,
/// то есть по порту Clock: событие, у которого время взято у системы, нельзя
/// проверить в тесте.
struct Envelope final {
    core::TenantId tenant;
    core::Instant occurred_at;
};

}  // namespace pdr::events
