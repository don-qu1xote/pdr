#pragma once

#include <string_view>

#include <userver/components/component_base.hpp>
#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/source.hpp>

#include "identity/contract.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/random_id_generator.hpp"
#include "infrastructure/userver_clock.hpp"

namespace pdr::identity {

/// ПРАВА СПРАШИВАЮТ ЗДЕСЬ: один компонент на весь процесс.
///
/// АДАПТЕРЫ НЕ КОНСТРУИРУЮТСЯ ВНУТРИ СЦЕНАРИЕВ, и это видно прямо в типах:
/// `PostgresRoleRepository` и его соседи принимают `ScopedTenantContext&` — они
/// живут внутри области арендатора и не переживают запрос. Компонент же живёт
/// всю жизнь сервиса. Поэтому здесь на каждый вопрос открывается своя область,
/// в ней собираются репозитории, и ответ уходит наружу значением.
///
/// Компонент лежит в СВОЁМ контексте, а не в сервисе: сборка политик из
/// адаптеров — знание identity, и процессу его знать незачем. Снаружи виден
/// `identity::Contract` — тот же заголовок, который видят чужие контексты.
/// Когда identity выделится в отдельный процесс, здесь появится сетевой
/// адаптер, а у спрашивающего не поменяется ни строки.
class PermissionsComponent final : public userver::components::ComponentBase, public Contract {
public:
    static constexpr std::string_view kName = "identity-permissions";

    PermissionsComponent(const userver::components::ComponentConfig& config,
                         const userver::components::ComponentContext& context);

    bool MayActFor(const core::TenantId& tenant,
                   const core::PersonId& actor,
                   const core::PersonId& student) const override;

    PolicyDecision Decide(const core::TenantId& tenant,
                          const core::PersonId& actor,
                          Action action,
                          const Resource& resource) const override;

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::UserverClock clock_;
    infrastructure::RandomIdGenerator ids_;
    userver::dynamic_config::Source configs_;
};

}  // namespace pdr::identity
