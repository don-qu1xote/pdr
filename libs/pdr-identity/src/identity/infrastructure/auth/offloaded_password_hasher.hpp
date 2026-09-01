#pragma once

#include <userver/engine/task/task_processor_fwd.hpp>

#include "identity/application/ports/password_hasher.hpp"

namespace pdr::identity {

/// СЧЁТ ПАРОЛЯ — НА ОТДЕЛЬНОМ ПРОЦЕССОРЕ ЗАДАЧ.
///
/// Argon2id занимает поток на десятки миллисекунд — так и задумано, в этом
/// весь смысл алгоритма. Но поток, занятый счётом, не отдаёт ответы всем
/// остальным: под нагрузкой входов растёт задержка ручек, которые про пароли
/// ничего не знают, и на графике это выглядит как «сервис тормозит».
///
/// Обёртка ничего не считает сама и знать про Argon2 не обязана: она переносит
/// работу на названный процессор задач и ждёт результата. Поэтому подменить
/// счёт в проверке по-прежнему можно, а забыть перенести — нельзя: в сервисе
/// собирается именно она.
class OffloadedPasswordHasher final : public ports::PasswordHasher {
public:
    OffloadedPasswordHasher(const ports::PasswordHasher& counting,
                            userver::engine::TaskProcessor& processor) noexcept;

    core::Result<PasswordHash> Hash(const Password& password,
                                    const PasswordRules& rules) const override;

    bool Matches(const Password& password, const PasswordHash& hash) const override;

private:
    const ports::PasswordHasher& counting_;
    userver::engine::TaskProcessor& processor_;
};

}  // namespace pdr::identity
