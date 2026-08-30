#include "identity/infrastructure/access/logging_configuration_faults.hpp"

#include <string>

#include <userver/logging/log.hpp>

namespace pdr::identity {

void LoggingConfigurationFaults::NoPolicyFor(Action action) const {
    LOG_ERROR() << "права: у действия «" << std::string{Name(action)}
                << "» нет политики. Действие запрещено всем, и это поломка настройки, "
                   "а не решение: свяжите его с политикой в identity::policies::PolicySet";
}

}  // namespace pdr::identity
