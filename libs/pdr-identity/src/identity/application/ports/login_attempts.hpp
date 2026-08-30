#pragma once

#include "core/types/ids.hpp"
#include "core/types/time.hpp"
#include "identity/core/digest.hpp"
#include "identity/core/login_throttle.hpp"

namespace pdr::identity::ports {

/// Счётчики неудачных попыток входа.
///
/// ХРАНЕНИЕ В БАЗЕ, А НЕ В ПАМЯТИ ПРОЦЕССА, и порт написан так, что иначе не
/// получится: `Register` не возвращает void — он отдаёт окно ПОСЛЕ учёта
/// попытки, то есть требует, чтобы прибавление и чтение были одной операцией.
/// В памяти одной реплики это выглядело бы одинаково, а работало бы на второй
/// реплике вдвое хуже и обнулялось бы перезапуском.
///
/// По чему считать — приходит отпечатком, а не почтой и не адресом: адрес это
/// персональные данные, и хранить их ради счётчика незачем.
class LoginAttempts {
public:
    LoginAttempts(const LoginAttempts&) = delete;
    LoginAttempts& operator=(const LoginAttempts&) = delete;

    virtual ~LoginAttempts() = default;

    /// Прибавить неудачу и вернуть окно, каким оно стало. Истёкшее окно
    /// начинается заново — это правило домена (`AttemptWindow::Registered`), и
    /// реализация обязана вести себя так же.
    virtual AttemptWindow Register(const core::TenantId& tenant,
                                   AttemptSubject subject,
                                   const Digest& of,
                                   core::Instant at,
                                   core::Instant::Duration window) = 0;

    /// Посмотреть окно, ничего не прибавляя: так спрашивают ДО проверки пароля.
    virtual AttemptWindow Seen(const core::TenantId& tenant,
                               AttemptSubject subject,
                               const Digest& of) const = 0;

    /// Забыть счёт. Зовётся после удачного входа: человек, вспомнивший пароль,
    /// не должен доживать окно вместе с тем, кто его перебирал.
    virtual void Forget(const core::TenantId& tenant, AttemptSubject subject, const Digest& of) = 0;

protected:
    LoginAttempts() = default;
};

}  // namespace pdr::identity::ports
