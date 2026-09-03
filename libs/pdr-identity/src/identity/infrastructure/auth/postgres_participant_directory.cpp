#include "identity/infrastructure/auth/postgres_participant_directory.hpp"

#include <optional>
#include <string>

#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/io/date.hpp>

#include "core/types/ids.hpp"

namespace pdr::identity {
namespace {

/// Идентификатор строки роли. Метки в общем списке нет намеренно: на выданную
/// роль не ссылается никто, идентификатор нужен только первичному ключу.
using RoleAssignmentId = core::StrongId<struct RoleAssignmentTag>;

}  // namespace

PostgresParticipantDirectory::PostgresParticipantDirectory(
    infrastructure::db::ScopedTenantContext& scope,
    const application::ports::IdGenerator& ids) noexcept
    : scope_{scope}, ids_{ids} {}

core::Result<void> PostgresParticipantDirectory::Enrol(const core::TenantId& tenant,
                                                       const ports::Enrolment& enrolment) {
    const auto& person = enrolment.person;

    const auto added = scope_.Session().Execute(
        sql::kIdentityPersonEnrol,
        tenant.ToString(),
        person.Id().ToString(),
        enrolment.display_name,
        person.Mail().has_value() ? std::optional<std::string>{person.Mail()->Value()}
                                  : std::nullopt,
        enrolment.zone.Name(),
        userver::storages::postgres::Date{person.BornOn().Year(),
                                          static_cast<int>(person.BornOn().Month()),
                                          static_cast<int>(person.BornOn().Day())});
    if (added.RowsAffected() == 0) {
        return core::Error{core::ErrorKind::kConflict,
                           "participant_email_taken",
                           "эта почта в кабинете уже занята"};
    }

    for (const auto role : kEveryRole) {
        if (!enrolment.roles.Has(role)) {
            continue;
        }
        scope_.Session().Execute(sql::kIdentityRoleAssignmentGrant,
                                 tenant.ToString(),
                                 ids_.Next<RoleAssignmentId>().ToString(),
                                 person.Id().ToString(),
                                 std::string{Name(role)});
    }
    return {};
}

bool PostgresParticipantDirectory::Knows(const core::TenantId&, const Email& mail) const {
    return !scope_.Session().Execute(sql::kIdentityPersonKnowsMail, mail.Value()).IsEmpty();
}

}  // namespace pdr::identity
