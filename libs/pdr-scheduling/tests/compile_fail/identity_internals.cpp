/// @file
/// Цель ОБЯЗАНА не собираться: scheduling включает внутренний заголовок
/// identity. Заголовок существует, но лежит в приватном каталоге чужого
/// модуля — в путях поиска этой цели его нет и не будет.
#include <optional>

#include "identity/contract.hpp"
#include "identity/core/guardianship.hpp"

int main() {
    return pdr::identity::Guardianship::Restore(
               pdr::core::TenantId::FromBytes(pdr::core::IdBytes{}),
               pdr::core::PersonId::FromBytes(pdr::core::IdBytes{}),
               pdr::core::PersonId::FromBytes(pdr::core::IdBytes{}),
               pdr::core::Instant::FromUnixMicros(0),
               std::nullopt)
                   .IsActive()
               ? 0
               : 1;
}
