#pragma once

#include "identity/core/password.hpp"

namespace pdr::identity::ports {

/// Счёт и проверка хеша пароля. Argon2id и ничего другого.
///
/// Проверка отдельным методом, а не сравнением двух хешей снаружи: параметры
/// счёта лежат внутри записи хеша, а не в конфиге на момент проверки, и
/// сравнивать надо ПО ТЕМ параметрам, с которыми хеш посчитали. Снаружи это
/// сделать нечем — и не нужно.
///
/// Сравнение внутри реализации обязано быть постоянным по времени: разница в
/// пару микросекунд между «первый байт не тот» и «последний байт не тот»
/// измеряется по сети, и этого хватает.
class PasswordHasher {
public:
    PasswordHasher(const PasswordHasher&) = delete;
    PasswordHasher& operator=(const PasswordHasher&) = delete;

    virtual ~PasswordHasher() = default;

    virtual core::Result<PasswordHash> Hash(const Password& password,
                                            const PasswordRules& rules) const = 0;

    virtual bool Matches(const Password& password, const PasswordHash& hash) const = 0;

protected:
    PasswordHasher() = default;
};

}  // namespace pdr::identity::ports
