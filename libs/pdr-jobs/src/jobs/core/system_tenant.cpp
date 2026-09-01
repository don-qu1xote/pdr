#include <stdexcept>

#include "jobs/contract.hpp"

namespace pdr::jobs {
namespace {

/// Тот же идентификатор, что в миграции V012__system_tenant.sql. Второй записи
/// значения в дереве нет: расхождение выглядело бы как «след не поставился».
constexpr std::string_view kSystemTenant = "00000000-0000-4000-8000-000000000001";

}  // namespace

core::TenantId SystemTenant() {
    const auto parsed = core::TenantId::Parse(kSystemTenant);
    if (!parsed.has_value()) {
        throw std::logic_error{
            "jobs: арендатор системы не разбирается — правьте вместе с "
            "миграцией V012__system_tenant.sql"};
    }
    return *parsed;
}

}  // namespace pdr::jobs
