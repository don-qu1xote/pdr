/// @file
/// Цель ОБЯЗАНА не собираться: область нельзя унести из места, где её открыли.
/// Соединение, пережившее свою область, — это соединение с чужим арендатором.
#include <optional>
#include <utility>

#include "core/types/ids.hpp"
#include "infrastructure/db/tenant_context.hpp"

pdr::infrastructure::db::TenantContext& Context();

std::optional<pdr::infrastructure::db::ScopedTenantContext> kept;

int main() {
    const auto tenant = pdr::core::TenantId::FromBytes(pdr::core::IdBytes{});

    auto scope = Context().Open(tenant);
    kept = std::move(scope);
    return 0;
}
