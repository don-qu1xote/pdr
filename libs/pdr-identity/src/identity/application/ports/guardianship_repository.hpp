#pragma once

#include <optional>
#include <vector>

#include "identity/core/guardianship.hpp"

namespace pdr::identity::ports {

/// Узкий порт: ровно то, что нужно сценариям опеки, и ничего больше.
///
/// Найти и сохранить здесь вместе не по недосмотру: это один агрегат и один
/// сценарий, которому нужны обе операции. Универсального Repository с двадцатью
/// методами в проекте нет.
class GuardianshipRepository {
public:
    GuardianshipRepository(const GuardianshipRepository&) = delete;
    GuardianshipRepository& operator=(const GuardianshipRepository&) = delete;

    virtual ~GuardianshipRepository() = default;

    /// Действующая опека между этими двумя, если она есть.
    virtual std::optional<Guardianship> FindActive(const core::TenantId& tenant,
                                                   const core::PersonId& guardian,
                                                   const core::PersonId& student) const = 0;

    /// Действующие опекуны этого ученика.
    ///
    /// Их может быть несколько — двое родителей живут в разных домах и оба
    /// платят за занятия, — и может не быть вовсе: у взрослого самостоятельного
    /// ученика опекуна нет. Без этого вопроса «сообщить опекуну» не выразить:
    /// сообщать некому, пока не спросили, кто это.
    virtual std::vector<core::PersonId> GuardiansOf(const core::TenantId& tenant,
                                                    const core::PersonId& student) const = 0;

    virtual void Save(const Guardianship& guardianship) = 0;

protected:
    GuardianshipRepository() = default;
};

}  // namespace pdr::identity::ports
