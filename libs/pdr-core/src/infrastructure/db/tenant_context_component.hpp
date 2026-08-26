#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>

#include "infrastructure/db/tenant_context.hpp"

namespace pdr::infrastructure::db {

/// Тонкий компонент: собирает единственную дверь к соединениям из кластера
/// Postgres и отдаёт её наружу ссылкой.
///
/// Компонентов «репозиторий такого-то контекста» больше не бывает: репозиторий
/// живёт внутри области арендатора и не переживает запрос, а компонент живёт
/// всю жизнь сервиса. Компонент отдаёт `TenantContext`, сценарий открывает
/// область и строит репозитории от неё.
///
/// Ничего доменного здесь нет и быть не должно — всё, что можно проверить без
/// сервиса, живёт в слоях ниже.
class TenantContextComponent final : public userver::components::ComponentBase {
public:
    static constexpr std::string_view kName = "tenant-context";

    TenantContextComponent(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context);

    TenantContext& Context() noexcept {
        return context_;
    }

private:
    TenantContext context_;
};

}  // namespace pdr::infrastructure::db
