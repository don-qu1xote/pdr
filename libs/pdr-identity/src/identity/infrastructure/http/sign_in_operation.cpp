#include "identity/infrastructure/http/sign_in_operation.hpp"

#include <stdexcept>
#include <string>
#include <utility>

#include <userver/components/component.hpp>
#include <userver/dynamic_config/storage/component.hpp>
#include <userver/formats/json/value_builder.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/yaml_config/merge_schemas.hpp>

#include "identity/application/sign_in.hpp"
#include "identity/infrastructure/auth/argon2_password_hasher.hpp"
#include "identity/infrastructure/auth/dynamic_config_auth_settings.hpp"
#include "identity/infrastructure/auth/offloaded_password_hasher.hpp"
#include "identity/infrastructure/auth/postgres_credential_store.hpp"
#include "identity/infrastructure/auth/postgres_login_attempts.hpp"
#include "identity/infrastructure/auth/postgres_session_store.hpp"
#include "identity/infrastructure/auth/sha256_digests.hpp"
#include "identity/infrastructure/http/session_transport.hpp"
#include "infrastructure/db/tenant_context_component.hpp"
#include "infrastructure/http/dynamic_config_key_lifetime.hpp"

namespace pdr::identity {
namespace {

constexpr std::string_view kSetCookie = "Set-Cookie";
constexpr std::string_view kUserAgent = "User-Agent";

core::Error NoTenantInAddress() {
    return core::Error{core::ErrorKind::kNotFound,
                       "cabinet_unknown",
                       "адрес не называет кабинет, в который входят"};
}

core::Error NotSignedIn() {
    return core::Error{
        core::ErrorKind::kValidation, "sign_in_refused", "почта или пароль не подошли"};
}

/// Отпечаток пришедшего: чем он смотрит и откуда. Хранится ОТПЕЧАТКАМИ, а не
/// самими значениями — счётчику попыток незачем держать чужой адрес, чтобы
/// прибавить единицу.
Fingerprint SeenBy(const userver::server::http::HttpRequest& request,
                   const ports::Digests& digests) {
    return Fingerprint{digests.Of(request.GetHeader(std::string{kUserAgent})),
                       digests.Of(request.GetRemoteAddress().PrimaryAddressString())};
}

/// Значение cookie сессии со всеми обязательными атрибутами. Атрибуты берутся
/// из транспорта, а не пишутся здесь второй раз.
std::string CookieFor(const SessionId& id) {
    return std::string{http::transport::kCookie} + "=" + id.ToString() + "; " +
           std::string{http::transport::kCookieAttributes};
}

}  // namespace

SignInDoor::SignInDoor(Database& database,
                       Keys& keys,
                       const application::ports::Clock& clock,
                       const application::ports::SecretGenerator& secrets,
                       userver::dynamic_config::Source configs,
                       userver::engine::TaskProcessor& counting,
                       pdr::http::KeyLifetime lifetime,
                       infrastructure::http::RequestSchema schema)
    : DoorHandler{database, keys, clock, lifetime, std::move(schema)},
      secrets_{secrets},
      configs_{configs},
      counting_{counting} {}

core::Result<core::TenantId> SignInDoor::Where(
    const userver::server::http::HttpRequest& request) const {
    const auto named = core::TenantId::Parse(request.GetPathArg(std::string{kTenantArgument}));
    if (!named.has_value()) {
        return NoTenantInAddress();
    }
    return *named;
}

core::Result<userver::formats::json::Value> SignInDoor::Run(const Call& call) const {
    const auto mail = Email::Parse(call.body["email"].As<std::string>());
    if (!mail.HasValue()) {
        return NotSignedIn();
    }

    const DynamicConfigAuthSettings settings{configs_};
    PostgresCredentialStore credentials{call.session};
    PostgresLoginAttempts attempts{call.session};
    PostgresSessionStore sessions{call.session};
    const Sha256Digests digests;
    const Argon2PasswordHasher counting{secrets_};
    const OffloadedPasswordHasher hasher{counting, counting_};

    const SignIn entering{
        settings, credentials, hasher, digests, attempts, sessions, secrets_, call.clock};

    const SignInRequest asked{call.caller.tenant,
                              mail.Value(),
                              call.body["password"].As<std::string>(),
                              SeenBy(call.request, digests),
                              http::ReadSessionId(call.request)};

    const auto opened = entering.Execute(asked);
    if (!opened.HasValue()) {
        return opened.Failure();
    }

    call.handed.emplace_back(std::string{kSetCookie}, CookieFor(opened.Value().Id()));

    userver::formats::json::ValueBuilder answer{userver::formats::json::Type::kObject};
    answer["expires_at"] = opened.Value().ExpiresAt().UnixMicros();
    return answer.ExtractValue();
}

SignInOperation::SignInOperation(const userver::components::ComponentConfig& config,
                                 const userver::components::ComponentContext& context)
    : infrastructure::http::OperationComponent{config, context},
      tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()},
      storage_{tenants_} {
    auto schema = infrastructure::http::RequestSchema::FromFile(config["schema"].As<std::string>());
    if (!schema.HasValue()) {
        throw std::runtime_error{"вход: схема тела не читается: " + schema.Failure().Detail()};
    }

    const auto configs = context.FindComponent<userver::components::DynamicConfig>().GetSource();
    const infrastructure::http::DynamicConfigKeyLifetime lifetimes{configs};
    const auto lifetime = lifetimes.Lifetime();
    if (!lifetime.HasValue()) {
        throw std::runtime_error{"вход: срок ключа повтора не годится: " +
                                 lifetime.Failure().Detail()};
    }

    door_.emplace(storage_,
                  keys_,
                  clock_,
                  secrets_,
                  configs,
                  context.GetTaskProcessor(config["counting-task-processor"].As<std::string>()),
                  lifetime.Value(),
                  std::move(schema).Value());
}

const infrastructure::http::Operation& SignInOperation::Handler() const {
    return *door_;
}

userver::yaml_config::Schema SignInOperation::GetStaticConfigSchema() {
    return userver::yaml_config::MergeSchemas<infrastructure::http::OperationComponent>(R"(
type: object
description: единственная дверь — та, у которой сессии ещё нет
additionalProperties: false
properties:
    schema:
        type: string
        description: путь к JSON-схеме тела запроса
    counting-task-processor:
        type: string
        description: процессор задач, на котором считается хеш пароля
)");
}

}  // namespace pdr::identity
