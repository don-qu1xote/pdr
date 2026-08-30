#include "identity/application/enrol_child.hpp"

#include "identity/core/guardianship.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/person.hpp"

namespace pdr::identity {

EnrolChild::EnrolChild(ports::ParticipantDirectory& directory,
                       ports::GuardianshipRepository& guardianships,
                       const application::ports::IdGenerator& ids,
                       const application::ports::Clock& clock) noexcept
    : directory_{directory}, guardianships_{guardianships}, ids_{ids}, clock_{clock} {}

core::Result<core::PersonId> EnrolChild::Execute(const EnrolChildRequest& request) const {
    const Person child{ids_.Next<core::PersonId>(), std::nullopt, request.born_on};
    const ports::Enrolment enrolment{
        child, RoleSet::Of({Role::kStudent}), request.display_name, request.zone};

    const auto enrolled = directory_.Enrol(request.tenant, enrolment);
    if (!enrolled) {
        return enrolled.Failure();
    }

    const auto guardianship =
        Guardianship::Grant(request.tenant, request.guardian, child.Id(), clock_.Now());
    if (!guardianship) {
        return guardianship.Failure();
    }

    guardianships_.Save(guardianship.Value());
    return child.Id();
}

}  // namespace pdr::identity
