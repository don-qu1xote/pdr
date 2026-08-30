#pragma once

#include <optional>

#include "identity/core/digest.hpp"
#include "identity/core/login_throttle.hpp"

namespace pdr::identity::ports {

/// Счётчик самостоятельных заведений с одного адреса.
///
/// В базе, а не в памяти процесса: в памяти он считает попытки одной реплики, а
/// со второй порог молча удваивается. Отдельно от `LoginAttempts` потому, что
/// счёт входов живёт в арендаторе, а заводящийся человек ещё ни в каком
/// арендаторе не состоит — считать его там негде.
class SignupAttempts {
public:
    SignupAttempts(const SignupAttempts&) = delete;
    SignupAttempts& operator=(const SignupAttempts&) = delete;

    virtual ~SignupAttempts() = default;

    virtual std::optional<AttemptWindow> Window(const Digest& address) const = 0;

    virtual void Save(const Digest& address, const AttemptWindow& window) = 0;

protected:
    SignupAttempts() = default;
};

}  // namespace pdr::identity::ports
