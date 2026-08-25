#pragma once

#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/contract.hpp"

namespace pdr::identity {

/// Реализация публичного контракта: то, что чужие контексты видят от identity.
///
/// Живёт внутри модуля, наружу отдаётся ссылкой на Contract. Сегодня вызов
/// прямой, внутри процесса; когда identity выделится в свой сервис, здесь
/// появится сетевой адаптер, а у вызывающего не поменяется ни строки.
class ContractService final : public Contract {
public:
    explicit ContractService(const ports::GuardianshipRepository& guardianships) noexcept;

    bool MayActFor(const core::TenantId& tenant,
                   const core::PersonId& actor,
                   const core::PersonId& student) const override;

private:
    const ports::GuardianshipRepository& guardianships_;
};

}  // namespace pdr::identity
