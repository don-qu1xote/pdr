#include "identity/infrastructure/auth/dynamic_config_auth_settings.hpp"

#include <chrono>
#include <string>

#include <userver/logging/log.hpp>

namespace pdr::identity {

const userver::dynamic_config::Key<PasswordRulesConfig> kPasswordRules{
    DynamicConfigAuthSettings::kPasswordVariable,
    userver::dynamic_config::DefaultAsJsonString{
        R"({"memory_kib": 19456, "iterations": 2, "parallelism": 1, "min_length": 10})"},
};

const userver::dynamic_config::Key<ThrottleConfig> kLoginThrottle{
    DynamicConfigAuthSettings::kThrottleVariable,
    userver::dynamic_config::DefaultAsJsonString{
        R"({"window_minutes": 15, "per_account": 10, "per_address": 50})"},
};

const userver::dynamic_config::Key<LifetimesConfig> kAuthLifetimes{
    DynamicConfigAuthSettings::kLifetimesVariable,
    userver::dynamic_config::DefaultAsJsonString{
        R"({"session_hours": 720, "invitation_hours": 168, "password_reset_minutes": 30})"},
};

namespace {

std::string Describe(const PasswordRulesConfig& value) {
    return "memory_kib=" + std::to_string(value.memory_kib) +
           " iterations=" + std::to_string(value.iterations) +
           " parallelism=" + std::to_string(value.parallelism) +
           " min_length=" + std::to_string(value.min_length);
}

std::string Describe(const ThrottleConfig& value) {
    return "window_minutes=" + std::to_string(value.window_minutes) +
           " per_account=" + std::to_string(value.per_account) +
           " per_address=" + std::to_string(value.per_address);
}

std::string Describe(const LifetimesConfig& value) {
    return "session_hours=" + std::to_string(value.session_hours) +
           " invitation_hours=" + std::to_string(value.invitation_hours) +
           " password_reset_minutes=" + std::to_string(value.password_reset_minutes);
}

template<class Config>
void Journal(std::string_view variable,
             const userver::dynamic_config::Diff& diff,
             const userver::dynamic_config::Key<Config>& key) {
    const auto current = Describe(diff.current[key]);
    if (!diff.previous.has_value()) {
        LOG_INFO() << std::string{variable} << ": первое применение — " << current;
        return;
    }

    const auto previous = Describe((*diff.previous)[key]);
    if (previous != current) {
        LOG_INFO() << std::string{variable} << ": было [" << previous << "], стало [" << current
                   << "]";
    }
}

}  // namespace

PasswordRulesConfig Parse(const userver::formats::json::Value& value,
                          userver::formats::parse::To<PasswordRulesConfig>) {
    return PasswordRulesConfig{value["memory_kib"].As<std::uint32_t>(),
                               value["iterations"].As<std::uint32_t>(),
                               value["parallelism"].As<std::uint32_t>(),
                               value["min_length"].As<std::uint32_t>()};
}

ThrottleConfig Parse(const userver::formats::json::Value& value,
                     userver::formats::parse::To<ThrottleConfig>) {
    return ThrottleConfig{value["window_minutes"].As<std::uint32_t>(),
                          value["per_account"].As<std::uint32_t>(),
                          value["per_address"].As<std::uint32_t>()};
}

LifetimesConfig Parse(const userver::formats::json::Value& value,
                      userver::formats::parse::To<LifetimesConfig>) {
    return LifetimesConfig{value["session_hours"].As<std::uint32_t>(),
                           value["invitation_hours"].As<std::uint32_t>(),
                           value["password_reset_minutes"].As<std::uint32_t>()};
}

DynamicConfigAuthSettings::DynamicConfigAuthSettings(userver::dynamic_config::Source source)
    : source_{source},
      journal_{source_.UpdateAndListen(
          this, "identity-auth-settings", &DynamicConfigAuthSettings::OnConfigUpdate)} {}

DynamicConfigAuthSettings::~DynamicConfigAuthSettings() {
    journal_.Unsubscribe();
}

void DynamicConfigAuthSettings::OnConfigUpdate(const userver::dynamic_config::Diff& diff) {
    Journal(kPasswordVariable, diff, kPasswordRules);
    Journal(kThrottleVariable, diff, kLoginThrottle);
    Journal(kLifetimesVariable, diff, kAuthLifetimes);
}

core::Result<PasswordRules> DynamicConfigAuthSettings::Passwords() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[kPasswordRules];
    return PasswordRules::Compose(
        value.memory_kib, value.iterations, value.parallelism, value.min_length);
}

core::Result<ThrottleLimits> DynamicConfigAuthSettings::Throttle() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[kLoginThrottle];
    return ThrottleLimits::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                       std::chrono::minutes{value.window_minutes}),
                                   value.per_account,
                                   value.per_address);
}

core::Result<AuthLifetimes> DynamicConfigAuthSettings::Lifetimes() const {
    const auto snapshot = source_.GetSnapshot();
    const auto& value = snapshot[kAuthLifetimes];
    return AuthLifetimes::Compose(std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::hours{value.session_hours}),
                                  std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::hours{value.invitation_hours}),
                                  std::chrono::duration_cast<core::Instant::Duration>(
                                      std::chrono::minutes{value.password_reset_minutes}));
}

}  // namespace pdr::identity
