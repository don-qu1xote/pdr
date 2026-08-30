#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/invite_participant.hpp"
#include "identity/application/ports/accounts.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/core/email.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity {

/// Дать ребёнку учётную запись — потом, когда понадобится.
///
/// Обратная сторона `EnrolChild`: сначала родитель заводит ребёнка без почты,
/// потом ребёнок подрастает и начинает распоряжаться своими занятиями сам. Тогда
/// ему называют адрес, и он получает ссылку — ту же самую одноразовую ссылку,
/// которой зовут любого участника.
///
/// Учётная запись при этом ОДНА НА ПЛОЩАДКУ: если по этому адресу человек уже
/// есть, вторая не заводится.
class AttachAccount final {
public:
    AttachAccount(const InviteParticipant& invitations,
                  const ports::Digests& digests,
                  ports::Accounts& accounts,
                  const application::ports::IdGenerator& ids,
                  const application::ports::Clock& clock) noexcept;

    core::Result<IssuedInvitation> Execute(const core::TenantId& tenant,
                                           const core::PersonId& child,
                                           const Email& mail) const;

private:
    const InviteParticipant& invitations_;
    const ports::Digests& digests_;
    ports::Accounts& accounts_;
    const application::ports::IdGenerator& ids_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
