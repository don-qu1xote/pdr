#pragma once

#include "identity/application/ports/digests.hpp"
#include "infrastructure/sha256_digests.hpp"

namespace pdr::identity {

/// Штатный SHA-256 userver — платформенный адаптер. Причина переезда — у
/// `identity::Digest`.
using Sha256Digests = infrastructure::Sha256Digests;

}  // namespace pdr::identity
