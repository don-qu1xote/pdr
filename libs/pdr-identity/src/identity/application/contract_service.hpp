#pragma once

#include "identity/application/policies/policy_set.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/role_repository.hpp"
#include "identity/contract.hpp"

namespace pdr::identity {

/// Реализация публичного контракта: то, что чужие контексты видят от identity.
///
/// Живёт внутри модуля, наружу отдаётся ссылкой на Contract. Сегодня вызов
/// прямой, внутри процесса; когда identity выделится в свой сервис, здесь
/// появится сетевой адаптер, а у вызывающего не поменяется ни строки.
///
/// СОБИРАЕТ СУБЪЕКТА И ОТДАЁТ ЕГО ПОЛИТИКЕ. Роли и опеку спрашивает здесь и
/// только здесь: политика от этого остаётся чистой функцией, а спрашивающий не
/// получает в руки ни ролей, ни связей — только ответ.
class ContractService final : public Contract {
public:
    ContractService(const ports::GuardianshipRepository& guardianships,
                    const ports::RoleRepository& roles,
                    const policies::PolicySet& permissions) noexcept;

    bool MayActFor(const core::TenantId& tenant,
                   const core::PersonId& actor,
                   const core::PersonId& student) const override;

    PolicyDecision Decide(const core::TenantId& tenant,
                          const core::PersonId& actor,
                          Action action,
                          const Resource& resource) const override;

private:
    bool Guards(const core::TenantId& tenant,
                const core::PersonId& actor,
                const Resource& resource) const;

    const ports::GuardianshipRepository& guardianships_;
    const ports::RoleRepository& roles_;
    const policies::PolicySet& permissions_;
};

}  // namespace pdr::identity
