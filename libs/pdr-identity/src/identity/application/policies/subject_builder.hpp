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

    /// Что человек может САМ в этот момент.
    ///
    /// Права ВЫЧИСЛЯЮТСЯ из даты рождения и порогов конфига, а не хранятся и не
    /// выдаются по заявке: колонка «может платить» устарела бы в полночь дня
    /// рождения. Дата рождения неизвестна — возможностей нет ни одной: за
    /// человека без даты по-прежнему действует опекун, и это безопасная
    /// сторона ошибки.
    Capabilities AbilityOf(const core::TenantId& tenant, const core::PersonId& person) const;

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
    /// Смотрит ли этот человек за тем, о ком ресурс.
    ///
    /// ДВА ОСНОВАНИЯ, ОДИН ОТВЕТ. Опека — у ребёнка; названный самим взрослым
    /// наблюдатель — у того, у кого опеки нет вовсе. Второе не выводится из
    /// первого: у совершеннолетнего ученика опекуна не существует, и требовать
    /// её от супруга, которого он сам назвал, значит требовать несуществующего.
    ///
    /// Согласия на основании опеки сюда НЕ считаются: они держатся опекой, и
    /// отозванная опека обязана обрывать доступ, сколько бы строк ни осталось.
    bool LooksAfter(const core::TenantId& tenant,
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
