#pragma once

#include <string_view>
#include <vector>

#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/invite_participant.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/one_time_tokens.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/core/contact_list.hpp"
#include "identity/core/membership.hpp"

namespace pdr::identity {

/// Одно выданное приглашение из списка: кому и что показать человеку.
struct SentInvitation final {
    Email mail;
    TokenSecret secret;
};

/// Что получилось у отправки списком.
struct InvitationRun final {
    ContactList judged;
    std::vector<SentInvitation> sent;
};

/// Позвать учеников СПИСКОМ.
///
/// У переезжающего репетитора двадцать учеников, и по одному он их звать не
/// станет — по одному он останется там, где они уже есть. Поэтому здесь
/// вставка списком, а не форма на один адрес.
///
/// ДВА ШАГА, И ПЕРВЫЙ НИЧЕГО НЕ ОТПРАВЛЯЕТ. `Preview` разбирает вставленное и
/// показывает по строкам, что будет: этих позовём, этот адрес не разобрался,
/// этот уже здесь, этому уже писали. `Send` отправляет то, что человек увидел.
/// Рассылки «сразу после импорта» здесь нет и не будет: письмо, ушедшее до
/// того, как отправитель увидел список, — это письмо, о котором он узнаёт из
/// ответов.
class InviteMany final {
public:
    InviteMany(const InviteParticipant& invitations,
               const ports::Digests& digests,
               const ports::OneTimeTokens& tokens,
               const ports::ParticipantDirectory& directory,
               const application::ports::Clock& clock) noexcept;

    /// Разобрать вставленное и рассудить каждую строку. НИЧЕГО НЕ ОТПРАВЛЯЕТ и
    /// ничего не пишет.
    ContactList Preview(const core::TenantId& tenant, std::string_view pasted) const;

    /// Отправить по рассуженному списку. Идёт только по строкам `ready`:
    /// человек видел ровно их.
    core::Result<InvitationRun> Send(const core::TenantId& tenant,
                                     Role role,
                                     const ContactList& previewed) const;

private:
    ContactList Judge(const core::TenantId& tenant, const ContactList& parsed) const;

    const InviteParticipant& invitations_;
    const ports::Digests& digests_;
    const ports::OneTimeTokens& tokens_;
    const ports::ParticipantDirectory& directory_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
