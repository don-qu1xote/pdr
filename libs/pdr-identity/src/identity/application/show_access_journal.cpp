#include "identity/application/show_access_journal.hpp"

#include <string>

namespace pdr::identity {

ShowAccessJournal::ShowAccessJournal(const Contract& permissions,
                                     const ports::AccessJournal& journal) noexcept
    : permissions_{permissions}, journal_{journal} {}

core::Result<std::vector<AccessRecord>> ShowAccessJournal::Execute(const core::TenantId& tenant,
                                                                   const core::PersonId& actor,
                                                                   const Resource& about,
                                                                   core::Instant since) const {
    if (!about.subject.has_value()) {
        return core::Error{core::ErrorKind::kValidation,
                           "journal_without_subject",
                           "журнал спрашивают о ком-то, а не вообще"};
    }

    const auto decision = permissions_.Decide(tenant, actor, Action::kViewAccessJournal, about);
    if (!decision.allowed) {
        return core::Error{core::ErrorKind::kForbidden,
                           "journal_not_yours",
                           "журнал доступа показывают тому, о ком он; причина отказа — " +
                               std::string{Name(decision.reason)}};
    }

    return journal_.AboutPerson(tenant, *about.subject, since);
}

}  // namespace pdr::identity
