#pragma once

#include <string>
#include <string_view>
#include <utility>

#include <userver/formats/json/serialize.hpp>
#include <userver/formats/json/value.hpp>
#include <userver/server/http/http_status.hpp>

#include "application/ports/clock.hpp"
#include "application/ports/idempotency_keys.hpp"
#include "application/ports/tenant_aware_repository.hpp"
#include "core/errors.hpp"
#include "core/idempotency.hpp"
#include "core/types/ids.hpp"
#include "identity/contract.hpp"
#include "infrastructure/http/error_mapping.hpp"
#include "infrastructure/http/fingerprint.hpp"
#include "infrastructure/http/idempotency.hpp"
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
    using Keys = pdr::http::ports::IdempotencyKeys<Session>;

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

        if (!pdr::http::Mutating(Translate(request.GetMethod()))) {
            return Answer(
                response, Plain(caller, body.Value(), request_id), occasion, std::nullopt);
        }

        const auto key =
            pdr::http::IdempotencyKey::Parse(request.GetHeader(std::string{kIdempotencyKeyHeader}));
        if (!key.HasValue()) {
            return Refuse(response, KeyRequired(kIdempotencyKeyHeader, occasion));
        }

        return Answer(response,
                      Guarded(caller,
                              body.Value(),
                              request_id,
                              key.Value(),
                              FingerprintOf(request.RequestBody())),
                      occasion,
                      key.Value());
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
                      Keys& keys,
                      const application::ports::Clock& clock,
                      pdr::http::KeyLifetime lifetime,
                      RequestSchema schema) noexcept
        : callers_{callers},
          database_{database},
          permissions_{permissions},
          keys_{keys},
          clock_{clock},
          lifetime_{lifetime},
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
    /// Чем кончилась область арендатора: что отдавать и выполнялась ли операция.
    ///
    /// Один тип на оба пути — меняющий и нет, — чтобы ответ собирался в одном
    /// месте. Хендлер, у которого два места сборки ответа, однажды забудет
    /// заголовок в одном из них.
    struct Served final {
        pdr::http::ClaimOutcome outcome{pdr::http::ClaimOutcome::kTaken};
        pdr::http::SavedAnswer answer;
    };

    /// Отказ, доведённый до конца области арендатора.
    ///
    /// Область завершается коммитом, если работа вернулась обычным путём
    /// (`PostgresTenantAwareRepository::Run`), — и для подавляющего большинства
    /// работ это правильно. Здесь другой случай: отказавшая операция обязана
    /// унести с собой И строку ключа. Иначе ключ остаётся занятым, а человек не
    /// может повторить, даже когда причина отказа исчезла: «слот занят»
    /// залипает на сутки.
    ///
    /// ИСКЛЮЧЕНИЕ ЗДЕСЬ НЕ СИГНАЛ ОБ ОШИБКЕ, А СПОСОБ ОТКАТИТЬ ТРАНЗАКЦИЮ.
    /// Другого у области нет: не позвали `Commit` — значит откат. Ловится оно
    /// на месте, в той же функции, что и бросается, и наружу не выходит; отказ
    /// снова становится значением сразу за границей области.
    struct Rollback final {
        core::Error refusal;
    };

    /// Обращение, которое ничего не меняет: ключ ему не нужен.
    core::Result<Served> Plain(const Caller& caller,
                               const userver::formats::json::Value& body,
                               const std::string& request_id) const {
        try {
            return database_.InTenant(caller.tenant, [&](Session& session) {
                return Served{pdr::http::ClaimOutcome::kTaken,
                              Produce(Call{caller, body, request_id, session})};
            });
        } catch (const Rollback& rolled) {
            return rolled.refusal;
        }
    }

    /// Обращение, которое меняет состояние.
    ///
    /// ОДНА ТРАНЗАКЦИЯ НА ВСЁ: занять ключ, выполнить операцию, записать ответ.
    /// Отдельная транзакция под ключ даёт ровно ту дырку, ради закрытия которой
    /// всё написано, — упали между ними, и либо операция прошла без ключа
    /// (повтор выполнит её второй раз), либо ключ занят без операции (повтор не
    /// выполнит её никогда). Здесь падение уносит и то и другое, и клиент
    /// повторяет с чистого места.
    core::Result<Served> Guarded(const Caller& caller,
                                 const userver::formats::json::Value& body,
                                 const std::string& request_id,
                                 const pdr::http::IdempotencyKey& key,
                                 const pdr::http::RequestFingerprint& fingerprint) const {
        const auto expires_at = lifetime_.ExpiresFrom(clock_.Now());

        try {
            return database_.InTenant(caller.tenant, [&](Session& session) {
                const auto claim =
                    Or(keys_.Take(session, caller.tenant, key, fingerprint, expires_at));
                if (claim.outcome != pdr::http::ClaimOutcome::kTaken) {
                    return Served{claim.outcome, claim.answer};
                }

                const auto answer = Produce(Call{caller, body, request_id, session});
                Or(keys_.Complete(session, caller.tenant, key, answer));
                return Served{pdr::http::ClaimOutcome::kTaken, answer};
            });
        } catch (const Rollback& rolled) {
            return rolled.refusal;
        }
    }

    /// Позвать сценарий и превратить его ответ в сохраняемый.
    pdr::http::SavedAnswer Produce(const Call& call) const {
        auto produced = Run(call);
        if (!produced.HasValue()) {
            throw Rollback{produced.Failure()};
        }
        return pdr::http::SavedAnswer{kOk, userver::formats::json::ToString(produced.Value())};
    }

    /// Значение или откат. Отказ внутри области не возвращается наружу
    /// значением: возвращённое значение — это коммит.
    template<class T>
    static T Or(const core::Result<T>& result) {
        if (!result.HasValue()) {
            throw Rollback{result.Failure()};
        }
        return result.Value();
    }

    static void Or(const core::Result<void>& result) {
        if (!result.HasValue()) {
            throw Rollback{result.Failure()};
        }
    }

    /// Сборка ответа — ОДНО место на все пути.
    template<class Response>
    std::string Answer(Response& response,
                       const core::Result<Served>& served,
                       const Occasion& occasion,
                       const std::optional<pdr::http::IdempotencyKey>& key) const {
        if (!served.HasValue()) {
            return Refuse(response, AsProblem(served.Failure(), occasion));
        }
        if (served.Value().outcome == pdr::http::ClaimOutcome::kInFlight) {
            return Refuse(response,
                          KeyInFlight(key.has_value() ? key->Value() : std::string{}, occasion));
        }

        response.SetStatus(
            static_cast<userver::server::http::HttpStatus>(served.Value().answer.status));
        response.SetHeader(std::string{"Content-Type"}, std::string{"application/json"});
        if (served.Value().outcome == pdr::http::ClaimOutcome::kReplay) {
            response.SetHeader(std::string{kReplayedHeader}, std::string{"true"});
        }
        return served.Value().answer.body;
    }

    template<class Response>
    static std::string Refuse(Response& response, const Problem& problem) {
        response.SetStatus(static_cast<userver::server::http::HttpStatus>(problem.status));
        response.SetHeader(std::string{"Content-Type"}, std::string{kProblemContentType});
        return Render(problem);
    }

    static constexpr int kOk = 200;

    const Callers& callers_;
    Database& database_;
    const identity::Contract& permissions_;
    Keys& keys_;
    const application::ports::Clock& clock_;
    pdr::http::KeyLifetime lifetime_;
    RequestSchema schema_;
};

}  // namespace pdr::infrastructure::http
