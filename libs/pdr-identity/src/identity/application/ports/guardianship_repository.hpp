#pragma once

#include <optional>

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

    virtual void Save(const Guardianship& guardianship) = 0;

protected:
    GuardianshipRepository() = default;
};

}  // namespace pdr::identity::ports
