#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

#include "application/ports/tenant_aware_repository.hpp"
#include "testing/check.hpp"
#include "testing/fake_tenant_aware_repository.hpp"

namespace {

using pdr::core::TenantId;
using pdr::testing::FakeTenantAwareRepository;
using pdr::testing::FakeTenantSession;

TenantId Tenant(std::uint8_t marker) {
    pdr::core::IdBytes bytes{};
    bytes[0] = marker;
    return TenantId::FromBytes(bytes);
}

/// Главный тест задачи: строка одного арендатора не видна под другим НИ ОДНИМ
/// запросом, включая выборку без условия.
void NothingOfAnotherTenantIsVisible() {
    FakeTenantAwareRepository repository;
    const auto first = Tenant(1);
    const auto second = Tenant(2);

    repository.InTenant(first, [](FakeTenantSession& session) { session.Insert("занятие А"); });
    repository.InTenant(second, [](FakeTenantSession& session) { session.Insert("занятие Б"); });

    const auto seen =
        repository.InTenant(second, [](FakeTenantSession& session) { return session.SelectAll(); });
    PDR_CHECK(seen.size() == 1);
    PDR_CHECK(seen.front() == "занятие Б");

    // Строка первого арендатора существует — просто из области второго её нет.
    // Без этой проверки тест был бы одинаково зелёным и на пустом хранилище.
    PDR_CHECK(repository.RowsBypassingPolicy().size() == 2);
}

/// Область объявляет арендатора раньше, чем работа задаёт первый вопрос.
void TenantIsDeclaredBeforeAnyQuery() {
    FakeTenantAwareRepository repository;
    PDR_CHECK(repository.Declarations() == 0);

    repository.InTenant(Tenant(1), [&repository](FakeTenantSession& session) {
        PDR_CHECK(repository.Declarations() == 1);
        PDR_CHECK(session.Tenant() == Tenant(1));
    });

    PDR_CHECK(repository.Declarations() == 1);
}

/// Вставка с чужим арендатором — отказ, а не строка, которую вставивший потом
/// не увидит. Это `with check` политики.
void ForeignRowCannotBeWritten() {
    FakeTenantAwareRepository repository;
    const auto first = Tenant(1);
    const auto second = Tenant(2);

    const auto accepted = repository.InTenant(first, [&second](FakeTenantSession& session) {
        return session.InsertFor(second, "чужое");
    });

    PDR_CHECK(!accepted);
    PDR_CHECK(repository.RowsBypassingPolicy().empty());
}

/// Изменение без условия трогает только своё: `delete from ...` под одним
/// арендатором не вычищает базу целиком.
void BareDeleteTouchesOnlyOwnRows() {
    FakeTenantAwareRepository repository;
    const auto first = Tenant(1);
    const auto second = Tenant(2);

    repository.InTenant(first, [](FakeTenantSession& session) {
        session.Insert("первое");
        session.Insert("второе");
    });
    repository.InTenant(second, [](FakeTenantSession& session) { session.Insert("чужое"); });

    const auto removed =
        repository.InTenant(first, [](FakeTenantSession& session) { return session.DeleteAll(); });
    PDR_CHECK(removed == 2);

    const auto left =
        repository.InTenant(second, [](FakeTenantSession& session) { return session.SelectAll(); });
    PDR_CHECK(left.size() == 1);
    PDR_CHECK(left.front() == "чужое");
    PDR_CHECK(repository.RowsBypassingPolicy().size() == 1);
}

/// Порт возвращает то, что вернула работа, — и ничего не возвращает, когда
/// работа ничего не возвращает. Обёртка вокруг единственного виртуального
/// метода обязана уметь оба случая, иначе сценарии начнут писать в захваченные
/// переменные.
void ScopeReturnsWhatTheWorkReturns() {
    FakeTenantAwareRepository repository;
    const auto tenant = Tenant(7);

    const auto counted = repository.InTenant(
        tenant, [](FakeTenantSession& session) { return session.SelectAll().size(); });
    PDR_CHECK(counted == 0);

    static_assert(std::is_void_v<decltype(repository.InTenant(
                      tenant, [](FakeTenantSession& session) { session.Insert("x"); }))>,
                  "работа без результата не должна заставлять сценарий что-то возвращать");
}

}  // namespace

int main() {
    NothingOfAnotherTenantIsVisible();
    TenantIsDeclaredBeforeAnyQuery();
    ForeignRowCannotBeWritten();
    BareDeleteTouchesOnlyOwnRows();
    ScopeReturnsWhatTheWorkReturns();
    return pdr::testing::Summary("core.tenant_isolation");
}
