#include "scheduling_service/secrets_guard.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <userver/components/component.hpp>
#include <userver/logging/log.hpp>
#include <userver/storages/secdist/component.hpp>

#include "application/verify_secrets.hpp"
#include "core/secrets.hpp"

namespace pdr::scheduling_service {

SecretsGuard::SecretsGuard(const userver::components::ComponentConfig& config,
                           const userver::components::ComponentContext& context)
    : ComponentBase{config, context},
      source_{std::in_place, context.FindComponent<userver::components::Secdist>()} {
    const auto checked = application::VerifySecrets(core::kEverySecret, *source_);
    if (!checked.HasValue()) {
        throw std::runtime_error{"сервис не поднимается: " + checked.Failure().Detail()};
    }

    LOG_INFO() << "секреты на месте: проверено " << core::kEverySecret.size();
}

}  // namespace pdr::scheduling_service
