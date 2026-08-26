#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/one_time_tokens.hpp"
#include "identity/core/membership.hpp"
#include "identity/core/one_time_token.hpp"

namespace pdr::identity {

/// Выданное приглашение. Секрет отдаётся ЗДЕСЬ И БОЛЬШЕ НИГДЕ: в базе остался
/// только отпечаток, и повторно показать ссылку нельзя даже нам. Потерянное
/// приглашение выдаётся заново, а не вспоминается.
struct IssuedInvitation final {
    OneTimeToken token;
    TokenSecret secret;
};

/// Пригласить участника: открытой регистрации ученика не существует.
///
/// Открытая регистрация в продукте, где учатся дети, означает, что завести
/// ученика может кто угодно и попасть он может к кому угодно. Здесь ученик
/// появляется только потому, что его позвал репетитор.
///
/// КТО ВПРАВЕ ПРИГЛАШАТЬ — здесь не решается. Это вопрос прав, и отвечает на
/// него PDR-IDENT-03; смешивать «кто это» и «что ему позволено» в одном
/// сценарии — верный способ получить проверку прав в половине мест.
class InviteParticipant final {
public:
    InviteParticipant(const ports::AuthSettings& settings,
                      const ports::Digests& digests,
                      ports::OneTimeTokens& tokens,
                      const application::ports::IdGenerator& ids,
                      const application::ports::SecretGenerator& secrets,
                      const application::ports::Clock& clock) noexcept;

    core::Result<IssuedInvitation> Execute(const core::TenantId& tenant, Role role) const;

private:
    const ports::AuthSettings& settings_;
    const ports::Digests& digests_;
    ports::OneTimeTokens& tokens_;
    const application::ports::IdGenerator& ids_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

/// Сколько байт случайности уходит в одноразовую ссылку. Тридцать два — это
/// 256 бит: перебором таких ссылок не занимаются.
inline constexpr std::size_t kTokenBytes = 32;

}  // namespace pdr::identity
