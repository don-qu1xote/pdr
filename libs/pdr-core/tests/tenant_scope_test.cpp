#include <type_traits>

#include <gtest/gtest.h>

#include "application/ports/tenant_aware_repository.hpp"
#include "builders/identifiers.hpp"
#include "fakes/fake_tenant_aware_repository.hpp"

/// @file
/// Механика порта области, а не поведение хранилища: что область возвращает и
/// сколько раз объявляет арендатора.
///
/// Само поведение — «чужого не видно», «чужую строку не вставить» — живёт в
/// contract-наборе `pdr/testing/repository_contract.hpp` и проверяется у ВСЕХ
/// реализаций сразу, а не у одного фейка.

namespace pdr::application::ports {
namespace {

using pdr::testing::FakeTenantAwareRepository;
using pdr::testing::FakeTenantSession;
using pdr::testing::Numbered;

TEST(TenantScope, DeclaresTenantBeforeTheWorkAsksAnything) {
    FakeTenantAwareRepository repository;
    EXPECT_EQ(repository.Declarations(), 0);

    repository.InTenant(
        Intent::kChanging, Numbered<core::TenantId>(1), [&repository](FakeTenantSession& session) {
            EXPECT_EQ(repository.Declarations(), 1) << "работа началась до объявления арендатора";
            EXPECT_TRUE(session.Tenant() == Numbered<core::TenantId>(1));
        });

    EXPECT_EQ(repository.Declarations(), 1);
}

TEST(TenantScope, EachScopeDeclaresAnew) {
    FakeTenantAwareRepository repository;

    repository.InTenant(Intent::kChanging, Numbered<core::TenantId>(1), [](FakeTenantSession&) {});
    repository.InTenant(Intent::kChanging, Numbered<core::TenantId>(2), [](FakeTenantSession&) {});

    EXPECT_EQ(repository.Declarations(), 2);
}

TEST(TenantScope, ReturnsWhatTheWorkReturnsAndNothingWhenItReturnsNothing) {
    FakeTenantAwareRepository repository;
    const auto tenant = Numbered<core::TenantId>(7);

    const auto counted =
        repository.InTenant(Intent::kReading, tenant, [](FakeTenantSession& session) {
            return session.SelectAll().size();
        });
    EXPECT_EQ(counted, 0U);

    static_assert(
        std::is_void_v<decltype(repository.InTenant(
            Intent::kChanging, tenant, [](FakeTenantSession& session) { session.Insert("x"); }))>,
        "работа без результата не должна заставлять сценарий что-то возвращать");
}

}  // namespace
}  // namespace pdr::application::ports
