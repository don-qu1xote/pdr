/// @file
/// Цель ОБЯЗАНА не собираться: сессия хранилища существует только внутри
/// области, объявившей арендатора, и получить её иначе — ошибка компиляции.
#include <vector>

#include "core/types/ids.hpp"
#include "fakes/fake_tenant_aware_repository.hpp"

int main() {
    std::vector<pdr::testing::FakeRow> rows;
    const auto tenant = pdr::core::TenantId::FromBytes(pdr::core::IdBytes{});

    pdr::testing::FakeTenantSession session{rows, tenant};
    session.Insert("мимо области");
    return 0;
}
