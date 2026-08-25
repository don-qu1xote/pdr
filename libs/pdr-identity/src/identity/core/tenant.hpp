#pragma once

#include <string>
#include <utility>

#include "core/errors.hpp"
#include "core/types/ids.hpp"

namespace pdr::identity {

/// Арендатор: репетитор-одиночка или школа.
///
/// Имя проверяется здесь, а не «в форме же виднее»: пустое имя арендатора
/// доходит до писем, счетов и печатных документов, и там его уже никто не
/// чинит. То же ограничение стоит в схеме — `identity_tenant_name_not_blank`.
class Tenant final {
public:
    static core::Result<Tenant> Compose(core::TenantId id, std::string name);

    const core::TenantId& Id() const noexcept {
        return id_;
    }
    const std::string& Name() const noexcept {
        return name_;
    }

    friend bool operator==(const Tenant&, const Tenant&) = default;

private:
    Tenant(core::TenantId id, std::string name) noexcept
        : id_{std::move(id)}, name_{std::move(name)} {}

    core::TenantId id_;
    std::string name_;
};

}  // namespace pdr::identity
