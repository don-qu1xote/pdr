#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/consents.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/maturity_settings.hpp"
#include "identity/core/consent.hpp"

namespace pdr::identity {

struct GiveConsentRequest final {
    core::TenantId tenant;
    /// О ком согласие.
    core::PersonId subject;
    /// Кто его даёт: сам ученик или его опекун.
    core::PersonId given_by;
    ConsentKind kind;
    ConsentAction action;
};

/// Записать согласие.
///
/// ЗА РЕБЁНКА СОГЛАШАЕТСЯ ОПЕКУН, и это не оговорка в документе, а условие
/// записи: ребёнок сам за себя согласиться не может, и посторонний за него —
/// тоже. С первого возрастного порога ученик даёт согласие сам в той части, где
/// действует сам (docs/architecture/permissions.md).
///
/// Версия перечня не приходит параметром: её называет продукт, а не тот, кто
/// зовёт сценарий. Иначе клиент, отставший на версию, записал бы согласие на
/// старую редакцию, и мы бы об этом не узнали.
class GiveConsent final {
public:
    GiveConsent(ports::Consents& consents,
                const ports::PolicyVersions& versions,
                const ports::GuardianshipRepository& guardianships,
                const ports::BirthDates& birth_dates,
                const ports::MaturitySettings& maturity,
                application::ports::IdGenerator& ids,
                const application::ports::Clock& clock) noexcept;

    core::Result<ConsentRecord> Execute(const GiveConsentRequest& request) const;

private:
    ports::Consents& consents_;
    const ports::PolicyVersions& versions_;
    const ports::GuardianshipRepository& guardianships_;
    const ports::BirthDates& birth_dates_;
    const ports::MaturitySettings& maturity_;
    application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
