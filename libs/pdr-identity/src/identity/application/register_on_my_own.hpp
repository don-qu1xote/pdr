#pragma once

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/application/ports/accounts.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/signup_attempts.hpp"
#include "identity/core/account.hpp"
#include "identity/core/email.hpp"
#include "identity/core/one_time_token.hpp"

namespace pdr::identity {

/// Заведённая самостоятельно запись и секрет подтверждения. Секрет отдаётся
/// ЗДЕСЬ И БОЛЬШЕ НИГДЕ: в базе остался отпечаток.
struct SelfRegistration final {
    Account account;
    TokenSecret confirmation;
};

/// Завести учётную запись самому — второй путь входа.
///
/// ОБА ПУТИ ВЕДУТ В ОДНО СОСТОЯНИЕ. Пришёл по приглашению репетитора или
/// пришёл сам через подбор — получается одна и та же учётная запись, и дальше
/// его не отличить: связи с практиками появляются отдельно, и их может быть
/// сколько угодно, включая ноль. Двух разных «видов учеников» в системе не
/// заводится, иначе через полгода половина сценариев будет спрашивать, какой
/// это вид.
///
/// РЕГИСТРАЦИЯ СВОБОДНА, МОДЕРИРУЕТСЯ ВИДИМОСТЬ. Очереди на разбор здесь нет:
/// она стоит на публикации практики в подборе (`AskToPublish`). Очередь на
/// входе означала бы, что репетитор с двадцатью учениками ждёт, пока его
/// посмотрят, — и уходит.
///
/// Антифрод здесь ровно двумя средствами: подтверждение почты и порог частоты
/// с одного адреса. Оба дешёвые и оба не мешают настоящему человеку.
class RegisterOnMyOwn final {
public:
    RegisterOnMyOwn(const ports::AuthSettings& settings,
                    const ports::Digests& digests,
                    ports::Accounts& accounts,
                    ports::SignupAttempts& attempts,
                    const application::ports::IdGenerator& ids,
                    const application::ports::SecretGenerator& secrets,
                    const application::ports::Clock& clock) noexcept;

    /// `from` — отпечаток того, откуда пришли: сетевой адрес считает и приносит
    /// адаптер, домену он не нужен и знать его незачем.
    core::Result<SelfRegistration> Execute(const Email& mail, const Digest& from) const;

private:
    const ports::AuthSettings& settings_;
    const ports::Digests& digests_;
    ports::Accounts& accounts_;
    ports::SignupAttempts& attempts_;
    const application::ports::IdGenerator& ids_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
