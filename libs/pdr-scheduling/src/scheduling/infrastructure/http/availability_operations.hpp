#pragma once

#include <string_view>

#include <pdr/api/openapi.hpp>

#include <userver/components/component_config.hpp>
#include <userver/components/component_context.hpp>
#include <userver/server/http/http_request.hpp>
#include <userver/yaml_config/schema.hpp>

#include "application/ports/id_generator.hpp"
#include "infrastructure/db/tenant_context.hpp"
#include "infrastructure/http/authorized_handler.hpp"
#include "infrastructure/http/operation.hpp"
#include "scheduling/infrastructure/http/parts.hpp"

namespace pdr::scheduling::http {

/// ПОКАЗАТЬ ДОСТУПНОСТЬ. Разобрать, спросить политику, позвать сценарий, отдать.
///
/// Тела у обращения нет — оно читающее, — и схема у него пустая: «здесь тела не
/// бывает» выражается пустым объектом, а не отсутствием схемы.
class GetAvailabilityHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Nothing,
                                                     api::Availability> {
public:
    explicit GetAvailabilityHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Nothing& body) const override;

    core::Result<api::Availability> Run(const Call& call) const override;

    const application::ports::IdGenerator& ids_;
};

/// ЗАПИСАТЬ ДОСТУПНОСТЬ ЦЕЛИКОМ.
class SetAvailabilityHandler final
    : public infrastructure::http::AuthorizedHandler<userver::server::http::HttpRequest,
                                                     infrastructure::db::ScopedTenantContext,
                                                     api::Availability,
                                                     api::Availability> {
public:
    explicit SetAvailabilityHandler(Parts& parts);

private:
    identity::Action Wants() const override;

    identity::Resource About(const userver::server::http::HttpRequest& request,
                             const infrastructure::http::Caller& caller,
                             const api::Availability& body) const override;

    core::Result<api::Availability> Run(const Call& call) const override;

    const application::ports::IdGenerator& ids_;
};

class GetAvailabilityOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-get-availability";

    GetAvailabilityOperation(const userver::components::ComponentConfig& config,
                             const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    GetAvailabilityHandler handler_;
};

class SetAvailabilityOperation final : public infrastructure::http::OperationComponent {
public:
    static constexpr std::string_view kName = "scheduling-set-availability";

    SetAvailabilityOperation(const userver::components::ComponentConfig& config,
                             const userver::components::ComponentContext& context);

    const infrastructure::http::Operation& Handler() const override;

    static userver::yaml_config::Schema GetStaticConfigSchema();

private:
    Parts parts_;
    SetAvailabilityHandler handler_;
};

}  // namespace pdr::scheduling::http
