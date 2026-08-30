#pragma once

#include <optional>

#include "core/types/ids.hpp"
#include "identity/core/email.hpp"
#include "identity/core/password.hpp"

namespace pdr::identity::ports {

/// Кому принадлежит хеш пароля.
struct Credential final {
    core::PersonId person;
    PasswordHash hash;
};

/// Хранилище паролей. Отдельно от людей: у человека пароля может не быть вовсе
/// — ученика завели, а по ссылке он ещё не пришёл, — и «человек без пароля» это
/// отсутствие строки, а не пустая колонка.
///
/// Поиск идёт по паре «арендатор и почта», а не по одной почте: почта у нас
/// уникальна ВНУТРИ арендатора (`identity_person_email_unique`), и глобального
/// поиска по ней не существует ни здесь, ни в схеме.
class CredentialStore {
public:
    CredentialStore(const CredentialStore&) = delete;
    CredentialStore& operator=(const CredentialStore&) = delete;

    virtual ~CredentialStore() = default;

    virtual std::optional<Credential> FindByEmail(const core::TenantId& tenant,
                                                  const Email& mail) const = 0;

    /// Хеш известного человека: так спрашивают при смене пароля, когда почта
    /// уже не при чём и человек взят из сессии.
    virtual std::optional<PasswordHash> FindFor(const core::TenantId& tenant,
                                                const core::PersonId& person) const = 0;

    virtual void Save(const core::TenantId& tenant,
                      const core::PersonId& person,
                      const PasswordHash& hash) = 0;

protected:
    CredentialStore() = default;
};

}  // namespace pdr::identity::ports
