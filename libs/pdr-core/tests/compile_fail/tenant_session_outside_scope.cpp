// Этот файл ОБЯЗАН не собираться. Сессия хранилища существует только внутри
// области, объявившей арендатора: получить её иначе — не «плохой стиль», а
// ошибка компиляции. Тест core.compile_fail.tenant_session_outside_scope
// проходит ровно тогда, когда сборка этой цели падает.
#include <vector>

#include "core/types/ids.hpp"
#include "testing/fake_tenant_aware_repository.hpp"

int main() {
    std::vector<pdr::testing::FakeRow> rows;
    const auto tenant = pdr::core::TenantId::FromBytes(pdr::core::IdBytes{});

    pdr::testing::FakeTenantSession session{rows, tenant};
    session.Insert("мимо области");
    return 0;
}
