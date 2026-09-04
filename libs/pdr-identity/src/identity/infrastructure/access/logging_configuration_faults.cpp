#include "identity/infrastructure/access/logging_configuration_faults.hpp"

#include <string>

#include <userver/logging/log.hpp>

#include "infrastructure/observe/log_fields.hpp"

namespace pdr::identity {

void LoggingConfigurationFaults::NoPolicyFor(Action action) const {
    LOG_ERROR() << "у действия нет политики. Действие запрещено всем, и это поломка настройки, "
                   "а не решение: свяжите его с политикой в identity::policies::PolicySet"
                << userver::logging::LogExtra{{{::pdr::infrastructure::observe::kPolicyActionField,
                                                std::string{Name(action)}}}};
}

}  // namespace pdr::identity
