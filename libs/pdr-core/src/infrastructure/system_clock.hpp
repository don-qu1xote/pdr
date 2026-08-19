#pragma once

#include "application/ports/clock.hpp"
#include "core/types/time.hpp"

namespace pdr::infrastructure {

/// Настоящие часы — единственное место во всём дереве, где спрашивают время у
/// системы. Слой infrastructure, и только он: в core и application такой вызов
/// не соберётся мимо ревью, его ловит scripts/check_layers.py.
class SystemClock final : public application::ports::Clock {
public:
    SystemClock() = default;

    core::Instant Now() const override;
};

}  // namespace pdr::infrastructure
