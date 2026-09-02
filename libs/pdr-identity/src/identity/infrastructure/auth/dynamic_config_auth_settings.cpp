#include "identity/infrastructure/auth/dynamic_config_auth_settings.hpp"

#include <chrono>
#include <string>
#include <string_view>

#include <dynamic_config/variables/PDR_AUTH_LIFETIMES.hpp>
#include <dynamic_config/variables/PDR_LOGIN_THROTTLE.hpp>
#include <dynamic_config/variables/PDR_SIGNUP_THROTTLE.hpp>
#include <dynamic_config/variables/PDR_SIGN_IN_RULES.hpp>

#include <userver/logging/log.hpp>

namespace pdr::identity {
namespace {

std::string Describe(const ::dynamic_config::pdr_sign_in_rules::VariableType& value) {
    return "memory_kib=" + std::to_string(value.memory_kib) +
           " iterations=" + std::to_string(value.iterations) +
           " parallelism=" + std::to_string(value.parallelism) +
           " min_length=" + std::to_string(value.min_length);
}

std::string Describe(const ::dynamic_config::pdr_login_throttle::VariableType& value) {
    return "window_minutes=" + std::to_string(value.window_minutes) +
           " per_account=" + std::to_string(value.per_account) +
           " per_address=" + std::to_string(value.per_address);
}

std::string Describe(const ::dynamic_config::pdr_signup_throttle::VariableType& value) {
    return "window_minutes=" + std::to_string(value.window_minutes) +
           " per_address=" + std::to_string(value.per_address);
}

std::string Describe(const ::dynamic_config::pdr_auth_lifetimes::VariableType& value) {
    return "session_hours=" + std::to_string(value.session_hours) +
           " invitation_hours=" + std::to_string(value.invitation_hours) +
           " password_reset_minutes=" + std::to_string(value.password_reset_minutes);
}

template<class Config>
void Journal(const userver::dynamic_config::Diff& diff,
             const userver::dynamic_config::Key<Config>& key) {
    const auto current = Describe(diff.current[key]);
    if (!diff.previous.has_value()) {
        LOG_INFO() << std::string{key.GetName()} << ": первое применение — " << current;
        return;
    }

    const auto previous = Describe((*diff.previous)[key]);
    if (previous != current) {
        LOG_INFO() << std::string{key.GetName()} << ": было [" << previous << "], стало ["
                   << current << "]";
    }
}

}  // namespace

DynamicConfigAuthSettings::DynamicConfigAuthSettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "identity-auth-settings", &DynamicConfigAuthSettings::OnConfigUpdate)} {}

DynamicConfigAuthSettings::~DynamicConfigAuthSettings() {
    journal_.Unsubscribe();
}

void DynamicConfigAuthSettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    Journal(diff, ::dynamic_config::PDR_SIGN_IN_RULES);
    Journal(diff, ::dynamic_config::PDR_LOGIN_THROTTLE);
    Journal(diff, ::dynamic_config::PDR_AUTH_LIFETIMES);
    Journal(diff, ::dynamic_config::PDR_SIGNUP_THROTTLE);
}

core::Result<PasswordRules> DynamicConfigAuthSettings::Passwords() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_SIGN_IN_RULES];
    return PasswordRules::Compose(
        value.memory_kib, value.iterations, value.parallelism, value.min_length);
}

core::Result<ThrottleLimits> DynamicConfigAuthSettings::Throttle() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_LOGIN_THROTTLE];
    return ThrottleLimits::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                       std::chrono::minutes{value.window_minutes}),
                                   value.per_account,
                                   value.per_address);
}

core::Result<SignupLimits> DynamicConfigAuthSettings::Signups() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_SIGNUP_THROTTLE];
    return SignupLimits::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                     std::chrono::minutes{value.window_minutes}),
                                 value.per_address);
}

core::Result<AuthLifetimes> DynamicConfigAuthSettings::Lifetimes() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[::dynamic_config::PDR_AUTH_LIFETIMES];
    return AuthLifetimes::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::hours{value.session_hours}),
                                  std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::hours{value.invitation_hours}),
                                  std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::minutes{value.password_reset_minutes}));
}

}  // namespace pdr::identity
