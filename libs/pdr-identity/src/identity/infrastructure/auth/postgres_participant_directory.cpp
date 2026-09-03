#include "identity/infrastructure/auth/postgres_participant_directory.hpp"

#include <optional>
#include <string>
#include <vector>

#include <pdr/sql_queries.hpp>

#include <userver/storages/postgres/io/date.hpp>

#include "core/types/ids.hpp"
#include "infrastructure/db/domain_types.hpp"

namespace pdr::identity {
namespace {

/// Идентификатор строки роли. Метки в общем списке нет намеренно: на выданную
/// роль не ссылается никто, идентификатор нужен только первичному ключу.
using RoleAssignmentId = core::StrongId<struct RoleAssignmentTag>;

/// Одна выдаваемая роль. Порядок полей — порядок массивов в
/// db/sql/identity/identity_role_assignment_grant.sql: штатный
/// `ExecuteDecomposeBulk` раскладывает структуру по колонкам ровно так.
struct RoleGrant final {
    core::TenantId tenant_id;
    RoleAssignmentId id;
    core::PersonId person_id;
    std::string role;
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
        sql::kIdentityPersonEnrol,
        tenant,
        person.Id(),
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

    std::vector<RoleGrant> granted;
    for (const auto role : kEveryRole) {
        if (!enrolment.roles.Has(role)) {
            continue;
        }
        granted.push_back(
            RoleGrant{tenant, ids_.Next<RoleAssignmentId>(), person.Id(), std::string{Name(role)}});
    }
    if (!granted.empty()) {
        scope_.Session().ExecuteDecomposeBulk(sql::kIdentityRoleAssignmentGrant, granted);
    }
    return {};
}

bool PostgresParticipantDirectory::Knows(const core::TenantId&, const Email& mail) const {
    return !scope_.Session().Execute(sql::kIdentityPersonKnowsMail, mail.Value()).IsEmpty();
}

}  // namespace pdr::identity
