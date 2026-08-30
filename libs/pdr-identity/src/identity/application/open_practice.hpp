#pragma once

#include <string>

#include "application/ports/clock.hpp"
#include "application/ports/id_generator.hpp"
#include "application/ports/secret_generator.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/application/ports/accounts.hpp"
#include "identity/application/ports/auth_settings.hpp"
#include "identity/application/ports/credential_store.hpp"
#include "identity/application/ports/digests.hpp"
#include "identity/application/ports/participant_directory.hpp"
#include "identity/application/ports/password_hasher.hpp"
#include "identity/application/ports/practices.hpp"
#include "identity/application/ports/session_store.hpp"
#include "identity/core/birth_date.hpp"
#include "identity/core/email.hpp"
#include "identity/core/session.hpp"

namespace pdr::identity {

/// Всё, что спрашивают у репетитора при заведении практики.
///
/// РАЗВИЛКИ ЗДЕСЬ НЕТ. Ни «зачем вы пришли», ни «хотите ли попасть в подбор»,
/// ни «репетитор вы или школа»: человек не обязан осмыслять вопрос, ответ на
/// который влияет непонятно на что (ADR-0016). Поля ровно те, без которых
/// практика не работает: чем входить, как называться, в каком часовом поясе
/// считать занятия.
struct OpenPracticeRequest final {
    std::string practice_name;
    std::string display_name;
    Email mail;
    BirthDate born_on;
    core::TimeZone zone;
    std::string password;
    Fingerprint seen;
};

/// Что получилось: практика, её владелец и сразу открытая сессия.
struct OpenedPractice final {
    core::TenantId tenant;
    core::PersonId owner;
    Session session;
};

/// Завести практику и сразу начать работать.
///
/// ГЛАВНЫЙ ПУТЬ ВХОДА, и он один. Репетитор с двадцатью учениками заводит
/// практику, зовёт их списком и работает — не увидев подбора ни разу.
/// Практика заводится СКРЫТОЙ: попадать в поиск ей незачем, пока хозяин сам об
/// этом не попросит, и «включить позже» — это переключатель в настройках, а не
/// вопрос на входе.
///
/// Владелец получает две роли сразу: он и хозяин практики, и репетитор в ней.
/// Репетитор-одиночка — обычный случай, а не школа с администратором, и
/// заставлять его заводить себе же вторую роль руками незачем.
///
/// Учётная запись человека при этом ОДНА НА ПЛОЩАДКУ: если он уже где-то есть
/// (учится сам или родитель у другого репетитора), практика заводится на того
/// же человека, а не на его двойника.
class OpenPractice final {
public:
    OpenPractice(const ports::AuthSettings& settings,
                 const ports::Digests& digests,
                 const ports::PasswordHasher& hasher,
                 ports::Accounts& accounts,
                 ports::Practices& practices,
                 ports::ParticipantDirectory& directory,
                 ports::CredentialStore& credentials,
                 ports::SessionStore& sessions,
                 const application::ports::IdGenerator& ids,
                 const application::ports::SecretGenerator& secrets,
                 const application::ports::Clock& clock) noexcept;

    core::Result<OpenedPractice> Execute(const OpenPracticeRequest& request) const;

private:
    const ports::AuthSettings& settings_;
    const ports::Digests& digests_;
    const ports::PasswordHasher& hasher_;
    ports::Accounts& accounts_;
    ports::Practices& practices_;
    ports::ParticipantDirectory& directory_;
    ports::CredentialStore& credentials_;
    ports::SessionStore& sessions_;
    const application::ports::IdGenerator& ids_;
    const application::ports::SecretGenerator& secrets_;
    const application::ports::Clock& clock_;
};

}  // namespace pdr::identity
