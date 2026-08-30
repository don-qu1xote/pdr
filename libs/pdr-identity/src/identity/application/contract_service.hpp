#pragma once

#include "identity/application/note_sensitive_access.hpp"
#include "identity/application/policies/policy_set.hpp"
#include "identity/application/policies/subject_builder.hpp"
#include "identity/contract.hpp"

namespace pdr::identity {

/// Реализация публичного контракта: то, что чужие контексты видят от identity.
///
/// Живёт внутри модуля, наружу отдаётся ссылкой на Contract. Сегодня вызов
/// прямой, внутри процесса; когда identity выделится в свой сервис, здесь
/// появится сетевой адаптер, а у вызывающего не поменяется ни строки.
///
/// СПРОСИТЬ — УЖЕ ЗНАЧИТ ОСТАВИТЬ СЛЕД. `Decide` для действий над содержанием
/// занятия пишет строку в журнал доступа — и когда пустил, и когда отказал.
/// Это не побочный эффект «заодно»: журнал, который заполняют отдельным
/// вызовом, через полгода имеет дыры ровно там, где смотрели молча. Обойти его
/// нечем, потому что обойти проверку прав нечем.
class ContractService final : public Contract {
public:
    ContractService(const policies::SubjectBuilder& subjects,
                    const policies::PolicySet& permissions,
                    const NoteSensitiveAccess& journal) noexcept;

    bool MayActFor(const core::TenantId& tenant,
                   const core::PersonId& actor,
                   const core::PersonId& student) const override;

    PolicyDecision Decide(const core::TenantId& tenant,
                          const core::PersonId& actor,
                          Action action,
                          const Resource& resource) const override;

private:
    const policies::SubjectBuilder& subjects_;
    const policies::PolicySet& permissions_;
    const NoteSensitiveAccess& journal_;
};

}  // namespace pdr::identity
