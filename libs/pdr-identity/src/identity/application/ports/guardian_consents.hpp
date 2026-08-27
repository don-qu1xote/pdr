#pragma once

#include <optional>
#include <vector>

#include "core/types/ids.hpp"
#include "identity/core/guardian_consent.hpp"

namespace pdr::identity::ports {

/// Узкий порт: согласия на уровни доступа опекуна.
///
/// Отдаётся СПИСОК действующих, а не «есть ли такой уровень»: правило
/// совершеннолетия смотрит на все уровни сразу и на то, кто их выдал, и
/// вопросом по одному уровню его не выразить.
///
/// Отозванные согласия наружу не отдаются: «был доступ» и «есть доступ» —
/// разные вопросы, и правами распоряжается второй. На первый отвечает журнал.
class GuardianConsents {
public:
    GuardianConsents(const GuardianConsents&) = delete;
    GuardianConsents& operator=(const GuardianConsents&) = delete;

    virtual ~GuardianConsents() = default;

    virtual std::vector<GuardianConsent> ActiveFor(const core::TenantId& tenant,
                                                   const core::PersonId& guardian,
                                                   const core::PersonId& student) const = 0;

    virtual std::optional<GuardianConsent> FindActive(const core::TenantId& tenant,
                                                      const core::PersonId& guardian,
                                                      const core::PersonId& student,
                                                      GuardianScope scope) const = 0;

    virtual void Save(const GuardianConsent& consent) = 0;

protected:
    GuardianConsents() = default;
};

}  // namespace pdr::identity::ports
