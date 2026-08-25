#pragma once

#include <string_view>

#include "core/types/ids.hpp"
#include "events/envelope.hpp"

namespace pdr::events::identity {

/// Опека отозвана: опекун больше не действует от имени ученика.
///
/// Издатель — контекст identity. Подписчик заводится в своём модуле и не
/// требует ни строчки правки здесь и у издателя.
struct GuardianshipRevoked final {
    static constexpr std::string_view kType = "identity.guardianship_revoked";
    static constexpr int kVersion = 1;

    Envelope envelope;
    core::PersonId guardian;
    core::PersonId student;
};

}  // namespace pdr::events::identity
