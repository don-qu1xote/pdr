#pragma once

#include <optional>

#include "identity/application/ports/birth_dates.hpp"
#include "infrastructure/db/tenant_context.hpp"

namespace pdr::identity {

/// Дата рождения в базе, `identity_person.born_on`.
///
/// Отдельный адаптер, а не поле в чтении человека целиком: возрасту нужна одна
/// колонка, и запрос, тянущий рядом почту и имя, — это ещё одно место, где
/// читают чужие контакты без повода.
class PostgresBirthDates final : public ports::BirthDates {
public:
    explicit PostgresBirthDates(infrastructure::db::ScopedTenantContext& scope) noexcept;

    std::optional<BirthDate> Of(const core::TenantId& tenant,
                                const core::PersonId& person) const override;

private:
    infrastructure::db::ScopedTenantContext& scope_;
};

}  // namespace pdr::identity
