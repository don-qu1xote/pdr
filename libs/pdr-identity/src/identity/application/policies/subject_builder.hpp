#pragma once

#include "application/ports/clock.hpp"
#include "identity/application/policies/subject.hpp"
#include "identity/application/ports/birth_dates.hpp"
#include "identity/application/ports/guardian_consents.hpp"
#include "identity/application/ports/guardianship_repository.hpp"
#include "identity/application/ports/maturity_settings.hpp"
#include "identity/application/ports/role_repository.hpp"

namespace pdr::identity::policies {

/// Собирает субъекта: роли, отношение к ресурсу и опекунские уровни.
///
/// ВСЯ РАБОТА С ХРАНИЛИЩЕМ — ЗДЕСЬ, и только здесь. Дальше идёт чистая
/// функция: политика получает готовый ответ и проверяется таблицей, а не
/// поднятым сервисом.
///
/// Отдельный класс, а не пять полей в `ContractService`: сборка субъекта —
/// самостоятельная работа с пятью источниками, и сложенная в контракт она
/// превращает его в место, куда дописывают всё подряд.
class SubjectBuilder final {
public:
    SubjectBuilder(const ports::GuardianshipRepository& guardianships,
                   const ports::RoleRepository& roles,
                   const ports::GuardianConsents& consents,
                   const ports::BirthDates& birth_dates,
                   const ports::MaturitySettings& maturity,
                   const application::ports::Clock& clock) noexcept;

    Subject For(const core::TenantId& tenant,
                const core::PersonId& actor,
                const Resource& resource) const;

    /// Кем спрашивающий приходится ресурсу. Отдельно — затем, что на вопрос
    /// «вправе ли он действовать за ученика» отвечает то же отношение, и второе
    /// его вычисление разошлось бы с первым.
    Tie TieFor(const core::TenantId& tenant,
               const core::PersonId& actor,
               const Resource& resource) const;

    /// Что открыто опекуну прямо сейчас. Нужно и объявлению о передаче прав, и
    /// сборке субъекта — считается одинаково.
    ///
    /// Негодное правило совершеннолетия закрывает всё: пустой набор запрещает
    /// каждый уровень, а причина видна там, где читают конфиг. Открыть доступ
    /// по сломанной настройке было бы хуже отказа — отказ человек заметит и
    /// пожалуется, а лишний доступ не заметит никто.
    GuardianAccess AccessOf(const core::TenantId& tenant,
                            const core::PersonId& guardian,
                            const core::PersonId& student) const;

private:
    bool Guards(const core::TenantId& tenant,
                const core::PersonId& actor,
                const Resource& resource) const;

    const ports::GuardianshipRepository& guardianships_;
    const ports::RoleRepository& roles_;
    const ports::GuardianConsents& consents_;
    const ports::BirthDates& birth_dates_;
    const ports::MaturitySettings& maturity_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity::policies
