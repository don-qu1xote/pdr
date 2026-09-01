#include "identity/infrastructure/component/permissions_component.hpp"

#include <utility>

#include <userver/components/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>

#include "identity/application/contract_service.hpp"
#include "identity/application/note_sensitive_access.hpp"
#include "identity/application/policies/policy_set.hpp"
#include "identity/application/policies/subject_builder.hpp"
#include "identity/infrastructure/access/dynamic_config_maturity_settings.hpp"
#include "identity/infrastructure/access/logging_configuration_faults.hpp"
#include "identity/infrastructure/access/postgres_birth_dates.hpp"
#include "identity/infrastructure/access/postgres_guardian_consents.hpp"
#include "identity/infrastructure/access/postgres_guardianships.hpp"
#include "identity/infrastructure/access/postgres_role_repository.hpp"
#include "identity/infrastructure/audit/postgres_access_log.hpp"
#include "infrastructure/db/tenant_context_component.hpp"

namespace pdr::identity {
namespace {

/// Всё, что нужно для одного вопроса о правах, собранное внутри области
/// арендатора. Живёт ровно столько, сколько живёт вопрос.
class AskedInScope final {
public:
    AskedInScope(infrastructure::db::ScopedTenantContext& scope,
                 const application::ports::Clock& clock,
                 const application::ports::IdGenerator& ids,
                 userver::dynamic_config::Source configs)
        : roles_{scope},
          consents_{scope},
          birth_dates_{scope},
          guardianships_{scope, ids},
          maturity_{std::move(configs)},
          log_{scope, ids},
          journal_{log_, clock},
          subjects_{guardianships_, roles_, consents_, birth_dates_, maturity_, clock},
          permissions_{faults_},
          contract_{subjects_, permissions_, journal_} {}

    const Contract& Asked() const noexcept {
        return contract_;
    }

private:
    PostgresRoleRepository roles_;
    PostgresGuardianConsents consents_;
    PostgresBirthDates birth_dates_;
    PostgresGuardianships guardianships_;
    DynamicConfigMaturitySettings maturity_;
    PostgresAccessLog log_;
    NoteSensitiveAccess journal_;
    LoggingConfigurationFaults faults_;
    policies::SubjectBuilder subjects_;
    policies::PolicySet permissions_;
    ContractService contract_;
};

}  // namespace

PermissionsComponent::PermissionsComponent(const userver::components::ComponentConfig& config,
                                           const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()},
      configs_{context.FindComponent<userver::components::DynamicConfig>().GetSource()} {}

bool PermissionsComponent::MayActFor(const core::TenantId& tenant,
                                     const core::PersonId& actor,
                                     const core::PersonId& student) const {
    auto scope = tenants_.Open(tenant);
    const AskedInScope asked{scope, clock_, ids_, configs_};

    const bool may = asked.Asked().MayActFor(tenant, actor, student);
    scope.Commit();
    return may;
}

PolicyDecision PermissionsComponent::Decide(const core::TenantId& tenant,
                                            const core::PersonId& actor,
                                            Action action,
                                            const Resource& resource) const {
    auto scope = tenants_.Open(tenant);
    const AskedInScope asked{scope, clock_, ids_, configs_};

    const auto decision = asked.Asked().Decide(tenant, actor, action, resource);
    scope.Commit();
    return decision;
}

}  // namespace pdr::identity
