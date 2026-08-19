// Этот файл ОБЯЗАН не собираться: scheduling лезет во внутренности identity.
// Заголовок ниже существует, но лежит в приватном каталоге чужого модуля — в
// путях поиска этой цели его нет, и не будет.
//
// Каталог compile_fail/ намеренно исключён из scripts/check_layers.py: здесь
// живёт заведомо неправильный код, и проверяет его компилятор.
#include "identity/contract.hpp"
#include "identity/core/guardianship.hpp"

int main() {
    return pdr::identity::Guardianship::Grant(pdr::core::TenantId::FromBytes(pdr::core::IdBytes{}),
                                              pdr::core::PersonId::FromBytes(pdr::core::IdBytes{}),
                                              pdr::core::PersonId::FromBytes(pdr::core::IdBytes{}),
                                              pdr::core::Instant::FromUnixMicros(0))
                   .IsActive()
               ? 0
               : 1;
}
