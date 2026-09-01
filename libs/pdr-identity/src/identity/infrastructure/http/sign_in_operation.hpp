#pragma once

#include <optional>
#include <string_view>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/dynamic_config/source.hpp>
#include <userver/engine/task/task_processor_fwd.hpp>
#include <userver/yaml_config/schema.hpp>

#include "core/idempotency.hpp"
#include "infrastructure/crypto_secret_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/operation.hpp"
#include "infrastructure/http/postgres_idempotency_keys.hpp"
#include "infrastructure/http/request_schema.hpp"
#include "infrastructure/postgres_tenant_aware_repository.hpp"
#include "infrastructure/userver_clock.hpp"

namespace pdr::identity {

/// ВХОД — единственная ручка-дверь на всю систему.
///
/// Сессии у этого запроса ещё нет: он её создаёт. Поэтому форма здесь другая —
/// `DoorHandler`, и её имя нарочно подозрительное: политику она не спрашивает.
/// Второй наследник двери в дереве ловится `scripts/check_http_form.py`.
///
/// Арендатор берётся ИЗ АДРЕСА КАБИНЕТА (`/cabinet/{tenant}/sign-in`), а не из
/// тела и не из cookie: почта уникальна ВНУТРИ арендатора, и «войти вообще» не
/// выражается. Справочника «поддомен — арендатор» в дереве нет, поэтому в
/// адресе стоит сам идентификатор; появится справочник — поменяется разбор
/// адреса, а не форма.
class SignInDoor final
    : public infrastructure::http::DoorHandler<userver::server::http::HttpRequest,
                                               infrastructure::db::ScopedTenantContext> {
public:
    /// Часть адреса, из которой берётся арендатор.
    static constexpr std::string_view kTenantArgument = "tenant";

    SignInDoor(Database& database,
               Keys& keys,
               const application::ports::Clock& clock,
               const application::ports::SecretGenerator& secrets,
               userver::dynamic_config::Source configs,
               userver::engine::TaskProcessor& counting,
               pdr::http::KeyLifetime lifetime,
               infrastructure::http::RequestSchema schema);

private:
    core::Result<core::TenantId> Where(
        const userver::server::http::HttpRequest& request) const override;

    core::Result<userver::formats::json::Value> Run(const Call& call) const override;

    const application::ports::SecretGenerator& secrets_;
    userver::dynamic_config::Source configs_;
    userver::engine::TaskProcessor& counting_;
};

/// Компонент-операция входа: собирает дверь один раз на всю жизнь процесса.
class SignInOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "identity-sign-in";

    SignInOperation(const userver::components::ComponentConfig& config,
                    const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    infrastructure::db::TenantContext& tenants_;
    infrastructure::PostgresTenantAwareRepository storage_;
    infrastructure::http::PostgresIdempotencyKeys keys_;
    infrastructure::UserverClock clock_;
    infrastructure::CryptoSecretGenerator secrets_;
    std::optional<SignInDoor> door_;
};

}  // namespace pdr::identity
