/// @file
/// Цель ОБЯЗАНА не собираться: область соединения открывается только с
/// арендатором. Забыть его — не ошибка внимательности, а несобирающийся код.
#include "infrastructure/db/tenant_context.hpp"

pdr::infrastructure::db::TenantContext& Context();

int main() {
    auto scope = Context().Open();
    return static_cast<int>(scope.Tenant().AsBytes()[0]);
}
