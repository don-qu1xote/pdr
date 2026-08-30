#include "identity/application/attach_account.hpp"

#include "identity/core/account.hpp"

namespace pdr::identity {

AttachAccount::AttachAccount(const InviteParticipant& invitations,
                             const ports::Digests& digests,
                             ports::Accounts& accounts,
                             const application::ports::IdGenerator& ids,
                             const application::ports::Clock& clock) noexcept
    : invitations_{invitations}, digests_{digests}, accounts_{accounts}, ids_{ids}, clock_{clock} {}

core::Result<IssuedInvitation> AttachAccount::Execute(const core::TenantId& tenant,
                                                      const core::PersonId& child,
                                                      const Email& mail) const {
    const auto digest = digests_.Of(mail.Value());
    const auto existing = accounts_.FindByMail(digest);
    if (existing.has_value() && !(existing->Id() == child)) {
        return core::Error{core::ErrorKind::kConflict,
                           "account_belongs_to_another",
                           "этой почтой уже пользуется другой человек"};
    }

    if (!existing.has_value()) {
        accounts_.Save(Account::Invited(child, digest, clock_.Now()));
    }

    return invitations_.Execute(tenant, Role::kStudent, mail);
}

}  // namespace pdr::identity
