#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "infrastructure/http/request_body.hpp"
#include "infrastructure/http/request_id.hpp"
#include "infrastructure/http/security_headers.hpp"
#include "infrastructure/observe/span_tags.hpp"

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

/// ЧТО КОНВЕЙЕР УЖЕ СДЕЛАЛ ЗА РУЧКУ.
///
/// Звенья штатного конвейера (`server::middlewares`) отрабатывают до того, как
/// запрос дойдёт сюда, и складывают сделанное в `RequestContext`. Форма берёт
/// это готовым и не повторяет работу: след запроса не считается второй раз,
/// тело не разбирается второй раз, заголовок ключа не читается второй раз.
///
/// Своего конвейера в дереве нет и не будет: у штатного есть то, чего у своего
/// не бывает, — звенья настраиваются ПО КАЖДОЙ РУЧКЕ через статический конфиг,
/// а в комплекте уже идут трассировка, дедлайны, распаковка и учёт (ADR-0013).
///
/// ЧТО В ЗВЕНО НЕ ВЫНЕСЕНО. Занятие ключа повтора и открытие области арендатора
/// остаются здесь, и по одной и той же причине: и то и другое обязано лежать
/// внутри ОДНОЙ транзакции с самой операцией. Звено работает снаружи неё —
/// значит, звеном это выражается только неправильно.
struct Prepared final {
    std::string request_id;

    /// Сырое тело запроса. Схему по нему сверяет форма: схема — знание ручки,
    /// а не конвейера.
    std::string body;

    /// Ключ повтора, если он прислан и разобрался. Обязателен ли он — решает
    /// форма по методу: у звена нет причины знать, какой метод что меняет.
    std::optional<pdr::http::IdempotencyKey> key;
};

/// Что ручка просит добавить к УСПЕШНОМУ ответу, кроме тела.
///
/// Законный случай ровно один — cookie входа. Сессию выдаёт сервер, и положить
/// её в тело значит попросить клиента самому сделать её недоступной скрипту, а
/// он не сможет: `HttpOnly` ставится только заголовком.
///
/// Статуса здесь не бывает: его выбирает форма по таблице `error_mapping.cpp`,
/// и второго места, где он выбирается, в проекте нет. Повтор по тому же ключу
/// эти заголовки не воспроизводит — сохраняются тело и статус, а выданная
/// сессия у клиента уже есть с первого раза.
using Handed = std::vector<std::pair<std::string, std::string>>;

/// Кого пустили — или готовый отказ.
///
/// Отказ здесь уже в форме ответа, а не доменной ошибкой: «кто ты» и «тебе
/// нельзя» — разные новости и разные ответы (401 и 403), и родом `core::Error`
/// это не выражается.
struct Admission final {
    std::optional<Caller> caller;
    Problem refusal;
};

/// РУЧКА, ДОВЕДЁННАЯ ДО СТРОКИ ОТВЕТА: то, что держит маршрут.
///
/// Тип тела и тип ответа у каждой ручки свои — они порождены из схемы, — а
/// маршрут обязан держать их все одинаково. Поэтому у формы два лица: здесь
/// стёртое, которым пользуется процесс, ниже типизованное, которым пользуется
/// наследник.
///
/// Больше здесь ничего нет и не появится: всё, что можно решить, решает форма,
/// а не тот, кто её держит.
template<class Request>
class Handler {
public:
    Handler(const Handler&) = delete;
    Handler& operator=(const Handler&) = delete;

    virtual ~Handler() = default;

    virtual std::string Serve(const Request& request, const Prepared& done) const = 0;

    virtual std::string Serve(const Request& request) const = 0;

protected:
    Handler() = default;
};

/// ФОРМА ЗАПРОСА: ПОРЯДОК ШАГОВ ЗАДАН ЗДЕСЬ И ОДИН РАЗ.
///
/// Разобрать, пустить, открыть область арендатора, позвать сценарий, отдать
/// ответ — не выбирается наследником. Иначе через полгода одна ручка
/// спрашивает политику до открытия области, другая после, третья забывает
/// заголовки безопасности на пути отказа, и это выясняется по жалобе.
///
/// БИЗНЕС-ЛОГИКИ ЗДЕСЬ НЕТ И НЕ БЫВАЕТ. `Run` только зовёт сценарий из
/// application и отдаёт то, что он вернул. Если в наследнике появляется `if`
/// про предметную область — правило забыли положить в core, и чинится это там,
/// а не здесь.
///
/// НАСЛЕДНИКОВ У САМОЙ ФОРМЫ РОВНО ДВА, и оба живут в этом файле:
/// `AuthorizedHandler` — обычный случай, и `DoorHandler` — вход. Третьего не
/// заводят: «кого пускать» решает форма, а не ручка, и ручка, решающая это
/// сама, — дырка в правах, а не гибкость. Второй `DoorHandler` в дереве ловит
/// `scripts/check_http_form.py`.
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
///
/// ТЕЛО И ОТВЕТ — ПОРОЖДЁННЫЕ ТИПЫ, и это третий и четвёртый параметры:
///
///   `Body` — структура из `components/schemas` спецификации. Форма разбирает
///     тело в неё ОДИН РАЗ и отдаёт наследнику готовым; `formats::json::Value`
///     до ручки не доходит, поэтому и разбирать руками в ней нечего;
///   `Answer` — структура ответа оттуда же. Наследник возвращает её, а в строку
///     её превращает форма — порождённым сериализатором, а не сборкой полей.
///
/// Расхождение схемы и ручки после этого невыразимо: поля, которого нет в
/// схеме, нет и в структуре, и обращение к нему не компилируется.
template<class Request, class Session, class Body, class Answer>
class ServedHandler : public Handler<Request> {
public:
    using Database = application::ports::TenantAwareRepository<Session>;
    using Keys = pdr::http::ports::IdempotencyKeys<Session>;

    ServedHandler(const ServedHandler&) = delete;
    ServedHandler& operator=(const ServedHandler&) = delete;

    virtual ~ServedHandler() = default;

    /// Весь путь запроса, когда конвейер уже отработал. Возвращает тело ответа
    /// и проставляет статус и заголовки в ответе запроса.
    std::string Serve(const Request& request, const Prepared& done) const override {
        auto& response = request.GetHttpResponse();
        ApplySecurityHeaders(response);
        response.SetHeader(std::string{kRequestIdHeader}, done.request_id);

        const Occasion occasion{request.GetRequestPath(), done.request_id};
        const bool mutating = pdr::http::Mutating(Translate(request.GetMethod()));

        std::string field;
        const auto body =
            ParseBody<Body>(!mutating && done.body.empty() ? std::string_view{kNoBody}
                                                           : std::string_view{done.body},
                            field);
        if (!body.HasValue()) {
            return Refuse(response, Malformed(body.Failure(), field, occasion));
        }

        const auto admitted = Admit(request, body.Value(), occasion);
        if (!admitted.caller.has_value()) {
            return Refuse(response, admitted.refusal);
        }
        const Caller& caller = *admitted.caller;
        observe::TagActor(caller.actor);

        if (!mutating) {
            return Assemble(response,
                            Plain(request, caller, body.Value(), done.request_id),
                            occasion,
                            std::nullopt);
        }

        if (!done.key.has_value()) {
            return Refuse(response, KeyRequired(kIdempotencyKeyHeader, occasion));
        }

        return Assemble(response,
                        Guarded(request,
                                caller,
                                body.Value(),
                                done.request_id,
                                *done.key,
                                FingerprintOf(done.body)),
                        occasion,
                        done.key);
    }

    /// Тот же путь БЕЗ конвейера: ручка собирает `Prepared` сама.
    ///
    /// Остаётся ради проверки хендлера без поднятого сервиса — она даёт форму
    /// за миллисекунды и без базы, и это правильно. В живом процессе зовётся
    /// перегрузка выше: мимо этой проходит всё, что даёт конвейер, — дедлайны,
    /// распаковка, учёт.
    std::string Serve(const Request& request) const override {
        Prepared done{RequestIdOf(request), std::string{request.RequestBody()}, std::nullopt};

        const auto key =
            pdr::http::IdempotencyKey::Parse(request.GetHeader(std::string{kIdempotencyKeyHeader}));
        if (key.HasValue()) {
            done.key = key.Value();
        }

        return Serve(request, done);
    }

protected:
    /// Запрос, доведённый до сценария: всё разобрано, всё проверено, арендатор
    /// объявлен базе.
    struct Call final {
        const Caller& caller;

        /// Тело, уже разобранное в структуру из схемы. Полей, которых нет в
        /// схеме, здесь нет и взять неоткуда.
        const Body& body;
        const std::string& request_id;
        Session& session;

        /// Куда сложить заголовки успешного ответа. Пусто у всех ручек, кроме
        /// двери: ей нужно выдать cookie сессии.
        Handed& handed;

        /// Часы — портом, а не системным временем: сценарий, спрашивающий
        /// «сейчас» у операционной системы, не проверяется на протухание.
        const application::ports::Clock& clock;

        /// Сам запрос — ради транспортных фактов, которых в разобранном теле
        /// нет и быть не может: чем и откуда пришли, какое удостоверение
        /// принесли. ТЕЛО ОТСЮДА НЕ ЧИТАЮТ: оно уже разобрано и лежит в `body`,
        /// а второй разбор — это второй набор правил.
        const Request& request;
    };

    ServedHandler(Database& database,
                  Keys& keys,
                  const application::ports::Clock& clock,
                  pdr::http::KeyLifetime lifetime) noexcept
        : database_{database}, keys_{keys}, clock_{clock}, lifetime_{lifetime} {}

    /// КОГО ПУСКАТЬ. Решает форма — один из двух её наследников, — а не ручка.
    virtual Admission Admit(const Request& request,
                            const Body& body,
                            const Occasion& occasion) const = 0;

    /// Позвать сценарий. ЕДИНСТВЕННОЕ, что хендлер делает, — и он тут зовёт.
    virtual core::Result<Answer> Run(const Call& call) const = 0;

private:
    /// Чем кончилась область арендатора: что отдавать и выполнялась ли операция.
    ///
    /// Один тип на оба пути — меняющий и нет, — чтобы ответ собирался в одном
    /// месте. Хендлер, у которого два места сборки ответа, однажды забудет
    /// заголовок в одном из них.
    struct Served final {
        pdr::http::ClaimOutcome outcome{pdr::http::ClaimOutcome::kTaken};
        pdr::http::SavedAnswer answer;
        Handed handed;
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
    core::Result<Served> Plain(const Request& request,
                               const Caller& caller,
                               const Body& body,
                               const std::string& request_id) const {
        try {
            return database_.InTenant(
                application::ports::Intent::kReading, caller.tenant, [&](Session& session) {
                    Served served{};
                    served.answer = Produce(
                        Call{caller, body, request_id, session, served.handed, clock_, request});
                    return served;
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
    core::Result<Served> Guarded(const Request& request,
                                 const Caller& caller,
                                 const Body& body,
                                 const std::string& request_id,
                                 const pdr::http::IdempotencyKey& key,
                                 const pdr::http::RequestFingerprint& fingerprint) const {
        const auto expires_at = lifetime_.ExpiresFrom(clock_.Now());

        try {
            return database_.InTenant(
                application::ports::Intent::kChanging, caller.tenant, [&](Session& session) {
                    const auto claim =
                        Or(keys_.Take(session, caller.tenant, key, fingerprint, expires_at));
                    if (claim.outcome != pdr::http::ClaimOutcome::kTaken) {
                        return Served{claim.outcome, claim.answer, Handed{}};
                    }

                    Served served{};
                    served.answer = Produce(
                        Call{caller, body, request_id, session, served.handed, clock_, request});
                    Or(keys_.Complete(session, caller.tenant, key, served.answer));
                    return served;
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
        return pdr::http::SavedAnswer{kOk, ToJsonString(produced.Value())};
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
    std::string Assemble(Response& response,
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
        for (const auto& [name, value] : served.Value().handed) {
            response.SetHeader(name, value);
        }
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

    /// ТЕЛА У ЧИТАЮЩЕГО ОБРАЩЕНИЯ НЕТ, а разбор всё равно один на все ручки.
    ///
    /// `GET` тела не носит, и пустая строка разборщику не JSON: ручка получила
    /// бы `request_not_json` вместо ответа. Схема у такого обращения при этом
    /// есть и пустая (`components/schemas/Nothing`) — «здесь тела не бывает»
    /// выражается пустым объектом, а не отсутствием схемы, и присланное поле
    /// по-прежнему отвергается.
    ///
    /// Подставляется только НЕменяющему обращению с пустым телом: у `POST`
    /// пустое тело остаётся отказом, и отказом с прежним кодом.
    static constexpr std::string_view kNoBody = "{}";

    Database& database_;
    Keys& keys_;
    const application::ports::Clock& clock_;
    pdr::http::KeyLifetime lifetime_;
};

/// ОБЫЧНАЯ РУЧКА: пришедшего опознают, политику спрашивают.
///
/// Наследник заполняет три вопроса — чего хочет, над чем и что позвать; всё
/// остальное уже решено, и решено одинаково для всех ручек.
template<class Request, class Session, class Body, class Answer>
class AuthorizedHandler : public ServedHandler<Request, Session, Body, Answer> {
    using Form = ServedHandler<Request, Session, Body, Answer>;

public:
    using Database = typename Form::Database;
    using Keys = typename Form::Keys;

protected:
    using Call = typename Form::Call;

    AuthorizedHandler(const Callers& callers,
                      Database& database,
                      const identity::Contract& permissions,
                      Keys& keys,
                      const application::ports::Clock& clock,
                      pdr::http::KeyLifetime lifetime) noexcept
        : Form{database, keys, clock, lifetime}, callers_{callers}, permissions_{permissions} {}

    /// Чего эта ручка хочет. Спрашивается у политики, а не решается здесь.
    virtual identity::Action Wants() const = 0;

    /// Над чем. Собирается из адреса, из уже разобранного тела и из того, кто
    /// пришёл; хождений в базу здесь нет — до области арендатора ещё не дошли.
    ///
    /// АДРЕС ЗДЕСЬ НЕ ЛИШНИЙ. У читающего обращения тела не бывает вовсе, и
    /// ресурс оно называет путём и запросом — «расписание такого-то». Без
    /// запроса такая ручка спрашивала бы политику о том, чего в вопросе нет, и
    /// получала бы отказ всем, кроме владельца практики.
    virtual identity::Resource About(const Request& request,
                                     const Caller& caller,
                                     const Body& body) const = 0;

private:
    Admission Admit(const Request& request,
                    const Body& body,
                    const Occasion& occasion) const final {
        const auto where = callers_.Where();
        const auto who = callers_.Identify(request.GetCookie(std::string{where.cookie}),
                                           request.GetHeader(std::string{where.header}));
        if (!who.HasValue()) {
            return Admission{std::nullopt, Unidentified(who.Failure(), occasion)};
        }

        const Caller& caller = who.Value();
        const auto decision =
            permissions_.Decide(caller.tenant, caller.actor, Wants(), About(request, caller, body));
        if (!decision.allowed) {
            return Admission{std::nullopt, AsProblem(decision, occasion)};
        }

        return Admission{caller, Problem{}};
    }

    const Callers& callers_;
    const identity::Contract& permissions_;
};

/// ДВЕРЬ. Имя нарочно подозрительное: тут не спрашивают политику.
///
/// Наследник у неё ровно один на всю систему — вход. Требовать удостоверение у
/// того, кто пришёл ЗА удостоверением, значит не пускать никого никогда:
/// сессии у этого запроса ещё нет, её создаёт он сам.
///
/// Арендатор при этом известен и обязателен — он из адреса кабинета, а не из
/// cookie: почта уникальна ВНУТРИ арендатора, и «войти вообще» не выражается.
/// Человека же нет: им пришедший станет по итогу этого самого запроса, поэтому
/// `Caller::actor` у двери пуст, и читать его сценарию нечего.
///
/// Второй наследник ловится `scripts/check_http_form.py`: дверь, заведённая
/// «на время» у второй ручки, — это ручка без прав, о которой никто не помнит.
template<class Request, class Session, class Body, class Answer>
class DoorHandler : public ServedHandler<Request, Session, Body, Answer> {
    using Form = ServedHandler<Request, Session, Body, Answer>;

public:
    using Database = typename Form::Database;
    using Keys = typename Form::Keys;

protected:
    using Call = typename Form::Call;

    using Form::Form;

    /// Чей это кабинет. Отказ означает, что адрес не называет арендатора —
    /// и до сценария дело не доходит.
    virtual core::Result<core::TenantId> Where(const Request& request) const = 0;

private:
    Admission Admit(const Request& request,
                    const Body& body,
                    const Occasion& occasion) const final {
        static_cast<void>(body);

        const auto tenant = Where(request);
        if (!tenant.HasValue()) {
            return Admission{std::nullopt, Unidentified(tenant.Failure(), occasion)};
        }

        return Admission{Caller{tenant.Value(), core::PersonId::FromBytes(core::IdBytes{})},
                         Problem{}};
    }
};

}  // namespace pdr::infrastructure::http
