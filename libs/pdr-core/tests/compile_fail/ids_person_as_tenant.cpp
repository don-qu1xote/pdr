// Этот файл ОБЯЗАН не собираться: перепутанные местами идентификаторы — ошибка
// компиляции, а не то, что ловят на ревью. Тест core.ids.compile_fail.* проходит
// ровно тогда, когда сборка этой цели падает.
#include "core/types/ids.hpp"

namespace {

void GrantAccess(const pdr::core::TenantId&, const pdr::core::PersonId&) {}

}  // namespace

int main() {
    const auto tenant = pdr::core::TenantId::FromBytes(pdr::core::IdBytes{});
    const auto person = pdr::core::PersonId::FromBytes(pdr::core::IdBytes{});

    GrantAccess(person, tenant);
    return 0;
}
