#include "identity/infrastructure/auth/postgres_participant_directory.hpp"

#include <string>

#include <userver/storages/postgres/io/date.hpp>
#include <userver/storages/postgres/query.hpp>

#include "core/types/ids.hpp"

namespace pdr::identity {
namespace {

/// Идентификатор строки роли. Метки в общем списке нет намеренно: на выданную
/// роль не ссылается никто, идентификатор нужен только первичному ключу.
using RoleAssignmentId = core::StrongId<struct RoleAssignmentTag>;

/// `on conflict do nothing` и проверка числа строк вместо отлова исключения:
/// занятая почта — ожидаемый отказ предметной области, а не авария. Ловить его
/// исключением значило бы разбирать код SQLSTATE и надеяться, что это был
/// именно тот уникальный ключ.
const userver::storages::postgres::Query kEnrolPerson{
    "INSERT INTO identity_person (tenant_id, id, display_name, email, tz, born_on) "
    "VALUES ($1::uuid, $2::uuid, $3, $4, $5, $6) "
    "ON CONFLICT (tenant_id, email) DO NOTHING",
    userver::storages::postgres::Query::Name{"identity_person_enrol"},
};

const userver::storages::postgres::Query kGrantRole{
    "INSERT INTO identity_role_assignment (tenant_id, id, person_id, role) "
    "VALUES ($1::uuid, $2::uuid, $3::uuid, $4)",
    userver::storages::postgres::Query::Name{"identity_role_assignment_grant"},
};

}  // namespace

PostgresParticipantDirectory::PostgresParticipantDirectory(
    infrastructure::db::ScopedTenantContext& scope,
    const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

core::Result<void> PostgresParticipantDirectory::Enrol(const core::TenantId& tenant,
                                                       const ports::Enrolment& enrolment) {
    const auto& person = enrolment.person;

    const auto added = scope_.Session().Execute(
        kEnrolPerson,
        tenant.ToString(),
        person.Id().ToString(),
        enrolment.display_name,
        person.Mail().Value(),
        enrolment.zone.Name(),
        userver::storages::postgres::Date{person.BornOn().Year(),
                                          static_cast<int>(person.BornOn().Month()),
                                          static_cast<int>(person.BornOn().Day())});
    if (added.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kConflict,
                           "participant_email_taken",
                           "эта почта в кабинете уже занята"};
    }

    scope_.Session().Execute(kGrantRole,
                             tenant.ToString(),
                             ids_.Next<RoleAssignmentId>().ToString(),
                             person.Id().ToString(),
                             std::string{Name(enrolment.role)});
    return {};
}

}  // namespace pdr::identity
