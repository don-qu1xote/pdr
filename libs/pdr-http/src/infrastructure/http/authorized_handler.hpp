#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/server/http/http_status.hpp>

#include "application/ports/tenant_aware_repository.hpp"
#include "core/errors.hpp"
#include "core/types/ids.hpp"
#include "identity/contract.hpp"
#include "infrastructure/http/error_mapping.hpp"
#include "infrastructure/http/problem.hpp"
#include "infrastructure/http/request_id.hpp"
#include "infrastructure/http/request_schema.hpp"
#include "infrastructure/http/security_headers.hpp"

namespace pdr::infrastructure::http {

/// Кто пришёл. Больше о человеке HTTP-слою знать нечего.
struct Caller final {
    core::TenantId tenant;
    core::PersonId actor;
};

/// Где транспорт держит удостоверение. Спрашивается у того, кто его проверяет:
/// имя cookie и порядок источников — знание про сессии, а не про HTTP.
struct CredentialSource final {
    std::string_view cookie;
    std::string_view header;
};

/// Опознание пришедшего. Узкий порт: HTTP-слой не знает ни про сессии, ни про
/// то, где они лежат, ни про то, чем они истекают.
class Callers {
public:
    Callers(const Callers&) = delete;
    Callers& operator=(const Callers&) = delete;

    virtual ~Callers() = default;

    virtual CredentialSource Where() const = 0;

    virtual core::Result<Caller> Identify(std::string_view cookie,
                                          std::string_view header) const = 0;

protected:
    Callers() = default;
};

/// Базовый хендлер: ФОРМА ЗАДАНА ЗДЕСЬ И ОДИН РАЗ.
///
/// Порядок шагов — разобрать, опознать, открыть область арендатора, спросить
/// политику, позвать сценарий, отдать ответ — не выбирается наследником. Он
/// заполняет три вопроса: чего хочет, над чем и что позвать; всё остальное уже
/// решено, и решено одинаково для всех ручек. Иначе через полгода одна ручка
/// спрашивает политику до открытия области, другая после, третья забывает
/// заголовки безопасности на пути отказа, и это выясняется по жалобе.
///
/// БИЗНЕС-ЛОГИКИ ЗДЕСЬ НЕТ И НЕ БЫВАЕТ. `Run` только зовёт сценарий из
/// application и отдаёт то, что он вернул. Если в наследнике появляется `if`
/// про предметную область — правило забыли положить в core, и чинится это там,
/// а не здесь.
///
/// Два параметра шаблона вместо своего слоя поверх userver:
///
///   `Request` — у `server::http::HttpRequest` уже есть всё нужное, а тестовый
///     двойник это тип с теми же методами (тот же приём, что у
///     `identity::http::ReadSessionId`);
///   `Session` — сессия хранилища. Порт `TenantAwareRepository<Session>`
///     обязуется объявить арендатора ДО первого запроса, то есть открыть
///     область (`db::ScopedTenantContext`) сам; на проде это транзакция
///     Postgres, в проверке — фейковая сессия с той же политикой.
template<class Request, class Session>
class AuthorizedHandler {
public:
    using Database = application::ports::TenantAwareRepository<Session>;

    AuthorizedHandler(const AuthorizedHandler&) = delete;
    AuthorizedHandler& operator=(const AuthorizedHandler&) = delete;

    virtual ~AuthorizedHandler() = default;

    /// Весь путь запроса. Возвращает тело ответа и проставляет статус и
    /// заголовки в ответе запроса.
    std::string Serve(Request& request) const {
        auto& response = request.GetHttpResponse();
        ApplySecurityHeaders(response);

        const std::string request_id = RequestIdOf(request);
        response.SetHeader(std::string{kRequestIdHeader}, request_id);

        const Occasion occasion{request.GetRequestPath(), request_id};

        std::string field;
        const auto body = schema_.Parse(request.RequestBody(), field);
        if (!body.HasValue()) {
            return Refuse(response, Malformed(body.Failure(), field, occasion));
        }

        const auto where = callers_.Where();
        const auto who = callers_.Identify(request.GetCookie(std::string{where.cookie}),
                                           request.GetHeader(std::string{where.header}));
        if (!who.HasValue()) {
            return Refuse(response, Unidentified(who.Failure(), occasion));
        }
        const Caller& caller = who.Value();

        const auto decision =
            permissions_.Decide(caller.tenant, caller.actor, Wants(), About(caller, body.Value()));
        if (!decision.allowed) {
            return Refuse(response, AsProblem(decision, occasion));
        }

        auto done = database_.InTenant(caller.tenant, [&](Session& session) {
            return Run(Call{caller, body.Value(), request_id, session});
        });
        if (!done.HasValue()) {
            return Refuse(response, AsProblem(done.Failure(), occasion));
        }

        response.SetHeader(std::string{"Content-Type"}, std::string{"application/json"});
        return userver::formats::json::ToString(done.Value());
    }

protected:
    /// Запрос, доведённый до сценария: всё разобрано, всё проверено, арендатор
    /// объявлен базе.
    struct Call final {
        const Caller& caller;
        const userver::formats::json::Value& body;
        const std::string& request_id;
        Session& session;
    };

    AuthorizedHandler(const Callers& callers,
                      Database& database,
                      const identity::Contract& permissions,
                      RequestSchema schema) noexcept
        : callers_{callers},
          database_{database},
          permissions_{permissions},
          schema_{std::move(schema)} {}

    /// Чего эта ручка хочет. Спрашивается у политики, а не решается здесь.
    virtual identity::Action Wants() const = 0;

    /// Над чем. Собирается из уже разобранного тела и из того, кто пришёл;
    /// хождений в базу здесь нет — до области арендатора ещё не дошли.
    virtual identity::Resource About(const Caller& caller,
                                     const userver::formats::json::Value& body) const = 0;

    /// Позвать сценарий. ЕДИНСТВЕННОЕ, что хендлер делает, — и он тут зовёт.
    virtual core::Result<userver::formats::json::Value> Run(const Call& call) const = 0;

private:
    template<class Response>
    static std::string Refuse(Response& response, const Problem& problem) {
        response.SetStatus(static_cast<userver::server::http::HttpStatus>(problem.status));
        response.SetHeader(std::string{"Content-Type"}, std::string{kProblemContentType});
        return Render(problem);
    }

    const Callers& callers_;
    Database& database_;
    const identity::Contract& permissions_;
    RequestSchema schema_;
};

}  // namespace pdr::infrastructure::http
