#include "identity/infrastructure/http/session_callers.hpp"

#include <string>

#include <userver/components/component.hpp>

#include "identity/application/authenticate_session.hpp"
#include "identity/infrastructure/auth/postgres_session_store.hpp"
#include "identity/infrastructure/http/session_transport.hpp"
#include "infrastructure/db/tenant_context_component.hpp"

namespace pdr::identity {
namespace {

/// Двойник запроса на два уже полученных значения.
///
/// Тот же приём, что у тестового двойника `ReadSessionId`: у транспорта ровно
/// два вопроса — cookie и заголовок, — и оба уже отвечены тем, кто спросил
/// `Where()`. Второго разбора порядка источников в дереве нет: он один и лежит
/// в `session_transport.hpp`.
class Brought final {
public:
    Brought(std::string_view cookie, std::string_view header) noexcept
        : cookie_{cookie}, header_{header} {}

    std::string_view GetCookie(const std::string&) const noexcept {
        return cookie_;
    }

    std::string_view GetHeader(const std::string&) const noexcept {
        return header_;
    }

private:
    std::string_view cookie_;
    std::string_view header_;
};

core::Error NotIdentified() {
    return core::Error{core::ErrorKind::kNotFound, "not_identified", "удостоверения в запросе нет"};
}

}  // namespace

CallersComponent::CallersComponent(const userver::components::ComponentConfig& config,
                                   const userver::components::ComponentContext& context)
    : userver::components::ComponentBase{config, context},
      tenants_{context.FindComponent<infrastructure::db::TenantContextComponent>().Context()} {}

infrastructure::http::CredentialSource CallersComponent::Where() const {
    return infrastructure::http::CredentialSource{http::transport::kCookie,
                                                  http::transport::kHeader};
}

core::Result<infrastructure::http::Caller> CallersComponent::Identify(
    std::string_view cookie, std::string_view header) const {
    const auto id = http::ReadSessionId(Brought{cookie, header});
    if (!id.has_value()) {
        return NotIdentified();
    }

    auto scope = tenants_.Open(id->Tenant());
    PostgresSessionStore sessions{scope};
    const AuthenticateSession checking{sessions, clock_};

    const auto session = checking.Execute(*id);
    scope.Commit();

    if (!session.HasValue()) {
        return session.Failure();
    }

    return infrastructure::http::Caller{session.Value().Tenant(), session.Value().Person()};
}

}  // namespace pdr::identity
