#pragma once

#include "application/ports/digests.hpp"
#include "identity/core/digest.hpp"

namespace pdr::identity::ports {

/// Счёт отпечатка — платформенный порт. Причина переезда — у `identity::Digest`.
using Digests = application::ports::Digests;

}  // namespace pdr::identity::ports
