#include "identity/application/invite_many.hpp"

namespace pdr::identity {

InviteMany::InviteMany(const InviteParticipant& invitations,
                       const ports::Digests& digests,
                       const ports::OneTimeTokens& tokens,
                       const ports::ParticipantDirectory& directory,
                       const application::ports::Clock& clock) noexcept
    : invitations_{invitations},
      digests_{digests},
      tokens_{tokens},
      directory_{directory},
      clock_{clock} {}

ContactList InviteMany::Judge(const core::TenantId& tenant, const ContactList& parsed) const {
    const auto now = clock_.Now();
    std::vector<Contact> judged;
    judged.reserve(parsed.Lines().size());

    for (const auto& line : parsed.Lines()) {
        if (line.Verdict() != ContactVerdict::kReady || !line.Mail().has_value()) {
            judged.push_back(line);
            continue;
        }

        const auto& mail = *line.Mail();
        if (directory_.Knows(tenant, mail)) {
            judged.push_back(line.Judged(ContactVerdict::kAlreadyEnrolled));
            continue;
        }
        if (tokens_.LiveInvitationTo(tenant, digests_.Of(mail.Value()), now).has_value()) {
            judged.push_back(line.Judged(ContactVerdict::kAlreadyInvited));
            continue;
        }

        judged.push_back(line);
    }

    return ContactList::Of(std::move(judged));
}

ContactList InviteMany::Preview(const core::TenantId& tenant, std::string_view pasted) const {
    return Judge(tenant, ContactList::Parse(pasted));
}

core::Result<InvitationRun> InviteMany::Send(const core::TenantId& tenant,
                                             Role role,
                                             const ContactList& previewed) const {
    const auto judged = Judge(tenant, previewed);

    std::vector<SentInvitation> sent;
    for (const auto& line : judged.Lines()) {
        if (line.Verdict() != ContactVerdict::kReady || !line.Mail().has_value()) {
            continue;
        }

        const auto issued = invitations_.Execute(tenant, role, line.Mail());
        if (!issued) {
            return issued.Failure();
        }
        sent.push_back(SentInvitation{*line.Mail(), issued.Value().secret});
    }

    return InvitationRun{judged, std::move(sent)};
}

}  // namespace pdr::identity
